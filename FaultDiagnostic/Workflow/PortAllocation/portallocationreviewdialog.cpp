#include "portallocationreviewdialog.h"
#include "ui_portallocationreviewdialog.h"

#include "../../../IODevices/portdefinitions.h"
#include "../../../logger.h"

#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTableWidgetItem>

namespace {
QString deviceKindName(JYDeviceKind kind)
{
    switch (kind) {
    case JYDeviceKind::PXIe5322:
        return QStringLiteral("PXIe5322");
    case JYDeviceKind::PXIe5323:
        return QStringLiteral("PXIe5323");
    case JYDeviceKind::PXIe5711:
        return QStringLiteral("PXIe5711");
    case JYDeviceKind::PXIe8902:
        return QStringLiteral("PXIe8902");
    }
    return QStringLiteral("Unknown");
}

QString portTypeName(TPSPortType type)
{
    switch (type) {
    case TPSPortType::CurrentOutput:
        return QStringLiteral("电流输出");
    case TPSPortType::VoltageOutput:
        return QStringLiteral("电压输出");
    case TPSPortType::CurrentInput:
        return QStringLiteral("电流输入");
    case TPSPortType::VoltageInput:
        return QStringLiteral("电压输入");
    case TPSPortType::DmmChannel:
        return QStringLiteral("万用表通道");
    }
    return QStringLiteral("Unknown");
}

QString resourcePrefix(JYDeviceKind kind)
{
    switch (kind) {
    case JYDeviceKind::PXIe5322:
        return QStringLiteral("PXIe5322");
    case JYDeviceKind::PXIe5323:
        return QStringLiteral("PXIe5323");
    case JYDeviceKind::PXIe5711:
        return QStringLiteral("PXIe5711");
    case JYDeviceKind::PXIe8902:
        return QStringLiteral("PXIe8902");
    }
    return QStringLiteral("Unknown");
}
}

PortAllocationReviewDialog::PortAllocationReviewDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PortAllocationReviewDialog)
{
    ui->setupUi(this);
    resize(1280, 760);

    ui->demandTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->demandTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->demandTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->demandTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    ui->availableTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->availableTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->availableTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->availableTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->availableTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    ui->availableTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);

    ui->selectedTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->selectedTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->selectedTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->selectedTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->selectedTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    ui->selectedTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);

    ui->mainSplitter->setStretchFactor(0, 3);
    ui->mainSplitter->setStretchFactor(1, 2);
    ui->mainSplitter->setSizes({760, 500});

    ui->demandTable->verticalHeader()->setDefaultSectionSize(28);
    ui->availableTable->verticalHeader()->setDefaultSectionSize(32);
    ui->selectedTable->verticalHeader()->setDefaultSectionSize(32);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &PortAllocationReviewDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &PortAllocationReviewDialog::reject);

    connect(ui->btnAutoAssign, &QPushButton::clicked, this, [this]() {
        bool changed = false;
        int changedCount = 0;

        auto findCandidateForType = [&](TPSPortType type) {
            for (int i = 0; i < m_availablePorts.size(); ++i) {
                if (!m_availablePorts[i].assigned && m_availablePorts[i].binding.type == type) {
                    return i;
                }
            }
            return -1;
        };

        for (int selectedIndex = 0; selectedIndex < m_selectedRows.size(); ++selectedIndex) {
            if (!m_selectedRows[selectedIndex].assigned) {
                const int candidateIndex = findCandidateForType(m_selectedRows[selectedIndex].binding.type);
                if (candidateIndex >= 0) {
                    assignCandidateToSelected(candidateIndex, selectedIndex);
                    changed = true;
                    changedCount += 1;
                }
            }
        }

        if (!changed) {
            QMessageBox::information(this, QStringLiteral("端口分配"), QStringLiteral("没有可自动分配的端口。"));
            return;
        }

        refreshAvailableTable();
        refreshSelectedTable();
        rebuildSummary();
        Logger::log(QStringLiteral("PortReview auto assign complete: changed=%1")
                        .arg(changedCount),
                    Logger::Level::Info);
    });

    connect(ui->btnClearSelection, &QPushButton::clicked, this, [this]() {
        int clearedCount = 0;
        for (auto &candidate : m_availablePorts) {
            candidate.assigned = false;
            candidate.selectedIndex = -1;
        }
        for (auto &selected : m_selectedRows) {
            if (selected.assigned) {
                clearedCount += 1;
            }
            selected.assigned = false;
            selected.binding.deviceKind = JYDeviceKind::PXIe5322;
            selected.binding.channel = -1;
            selected.binding.resourceId.clear();
        }
        refreshAvailableTable();
        refreshSelectedTable();
        rebuildSummary();
        Logger::log(QStringLiteral("PortReview clear all: cleared=%1")
                        .arg(clearedCount),
                    Logger::Level::Info);
    });
}

PortAllocationReviewDialog::~PortAllocationReviewDialog()
{
    delete ui;
}

void PortAllocationReviewDialog::setItems(const QVector<TaskPortAllocationReviewItem> &items)
{
    m_items = items;
    m_selectedRows.clear();
    m_availablePorts.clear();

    QMap<QString, int> availableIndexByResource;

    auto appendAllPorts = [&](TPSPortType type, JYDeviceKind kind, int start, int end) {
        for (int channel = start; channel <= end; ++channel) {
            const QString resourceId = QStringLiteral("%1.CH%2").arg(resourcePrefix(kind)).arg(channel);
            if (availableIndexByResource.contains(resourceId)) {
                continue;
            }

            CandidatePortEntry entry;
            entry.binding.type = type;
            entry.binding.deviceKind = kind;
            entry.binding.channel = channel;
            entry.binding.resourceId = resourceId;
            m_availablePorts.push_back(entry);
            availableIndexByResource.insert(resourceId, m_availablePorts.size() - 1);
        }
    };

    appendAllPorts(TPSPortType::CurrentOutput,
                   JYDeviceKind::PXIe5711,
                   PortDefinitions::JY5711Ports::Current_OUTPUT_START,
                   PortDefinitions::JY5711Ports::Current_OUTPUT_END);
    appendAllPorts(TPSPortType::VoltageOutput,
                   JYDeviceKind::PXIe5711,
                   PortDefinitions::JY5711Ports::Voltage_OUTPUT_START,
                   PortDefinitions::JY5711Ports::Voltage_OUTPUT_END);
    appendAllPorts(TPSPortType::VoltageInput,
                   JYDeviceKind::PXIe5322,
                   PortDefinitions::JY5322Ports::Voltage_INPUT_START,
                   PortDefinitions::JY5322Ports::Voltage_INPUT_END);
    appendAllPorts(TPSPortType::CurrentInput,
                   JYDeviceKind::PXIe5323,
                   PortDefinitions::JY5323Ports::Current_INPUT_START,
                   PortDefinitions::JY5323Ports::Current_INPUT_END);
    appendAllPorts(TPSPortType::DmmChannel,
                   JYDeviceKind::PXIe8902,
                   PortDefinitions::JY8902Ports::DMM_CHANNEL_START,
                   PortDefinitions::JY8902Ports::DMM_CHANNEL_END);

    for (int taskIndex = 0; taskIndex < m_items.size(); ++taskIndex) {
        const auto &item = m_items[taskIndex];
        const QString component = item.componentRef.trimmed().isEmpty()
            ? QStringLiteral("(未命名)")
            : item.componentRef.trimmed();

        for (int bindingIndex = 0; bindingIndex < item.bindings.size(); ++bindingIndex) {
            const TPSPortBinding binding = item.bindings[bindingIndex];

            SelectedBindingEntry selected;
            selected.taskIndex = taskIndex;
            selected.bindingIndex = bindingIndex;
            selected.componentRef = component;
            selected.binding = binding;
            selected.assigned = !binding.resourceId.trimmed().isEmpty();
            m_selectedRows.push_back(selected);

            const QVector<TPSPortBinding> candidates = candidatesForBinding(binding);
            for (const auto &candidate : candidates) {
                if (!availableIndexByResource.contains(candidate.resourceId)) {
                    CandidatePortEntry entry;
                    entry.binding = candidate;
                    m_availablePorts.push_back(entry);
                    availableIndexByResource.insert(candidate.resourceId, m_availablePorts.size() - 1);
                }
            }
        }
    }

    for (int selectedIndex = 0; selectedIndex < m_selectedRows.size(); ++selectedIndex) {
        auto &selected = m_selectedRows[selectedIndex];
        if (!selected.assigned) {
            continue;
        }

        const QString resourceId = selected.binding.resourceId.trimmed();
        if (resourceId.isEmpty()) {
            selected.assigned = false;
            continue;
        }

        int candidateIndex = availableIndexByResource.value(resourceId, -1);
        if (candidateIndex < 0) {
            CandidatePortEntry entry;
            entry.binding = selected.binding;
            m_availablePorts.push_back(entry);
            candidateIndex = m_availablePorts.size() - 1;
            availableIndexByResource.insert(resourceId, candidateIndex);
        }

        if (m_availablePorts[candidateIndex].assigned) {
            selected.assigned = false;
            selected.binding.resourceId.clear();
            selected.binding.channel = -1;
        } else {
            m_availablePorts[candidateIndex].assigned = true;
            m_availablePorts[candidateIndex].selectedIndex = selectedIndex;
        }
    }

    refreshAvailableTable();
    refreshSelectedTable();
    rebuildSummary();
    Logger::log(QStringLiteral("PortReview set items: taskCount=%1 selectedRows=%2 candidates=%3")
                    .arg(m_items.size())
                    .arg(m_selectedRows.size())
                    .arg(m_availablePorts.size()),
                Logger::Level::Info);
}

QVector<TaskPortAllocationReviewItem> PortAllocationReviewDialog::reviewedItems() const
{
    return m_items;
}

void PortAllocationReviewDialog::accept()
{
    for (const auto &selected : m_selectedRows) {
        if (!selected.assigned || selected.binding.resourceId.trimmed().isEmpty()) {
            QMessageBox::warning(this,
                                 QStringLiteral("端口分配"),
                                 QStringLiteral("仍有未分配端口：%1 / %2")
                                     .arg(selected.componentRef, selected.binding.identifier));
            return;
        }
    }

    for (const auto &selected : m_selectedRows) {
        if (selected.taskIndex < 0 || selected.taskIndex >= m_items.size()) {
            continue;
        }
        auto &bindings = m_items[selected.taskIndex].bindings;
        if (selected.bindingIndex < 0 || selected.bindingIndex >= bindings.size()) {
            continue;
        }
        bindings[selected.bindingIndex] = selected.binding;
    }

    QSet<QString> occupied;
    for (const auto &selected : m_selectedRows) {
        if (occupied.contains(selected.binding.resourceId)) {
            QMessageBox::warning(this,
                                 QStringLiteral("端口分配"),
                                 QStringLiteral("检测到端口冲突：%1 被重复分配。")
                                     .arg(selected.binding.resourceId));
        return;
    }
        occupied.insert(selected.binding.resourceId);
    }

    Logger::log(QStringLiteral("PortReview accepted: allocated=%1")
                    .arg(m_selectedRows.size()),
                Logger::Level::Info);

    QDialog::accept();
}

void PortAllocationReviewDialog::assignCandidateToSelected(int candidateIndex, int selectedIndex)
{
    if (candidateIndex < 0 || candidateIndex >= m_availablePorts.size()) {
        return;
    }
    if (selectedIndex < 0 || selectedIndex >= m_selectedRows.size()) {
        return;
    }

    auto &candidate = m_availablePorts[candidateIndex];
    auto &selected = m_selectedRows[selectedIndex];

    if (candidate.assigned || selected.assigned) {
        return;
    }
    if (candidate.binding.type != selected.binding.type) {
        return;
    }

    selected.binding.deviceKind = candidate.binding.deviceKind;
    selected.binding.channel = candidate.binding.channel;
    selected.binding.resourceId = candidate.binding.resourceId;
    selected.assigned = true;

    candidate.assigned = true;
    candidate.selectedIndex = selectedIndex;

    Logger::log(QStringLiteral("PortReview assign: component=%1 identifier=%2 resource=%3 channel=%4")
                    .arg(selected.componentRef)
                    .arg(selected.binding.identifier)
                    .arg(selected.binding.resourceId)
                    .arg(selected.binding.channel),
                Logger::Level::Info);
}

void PortAllocationReviewDialog::unassignSelected(int selectedIndex)
{
    if (selectedIndex < 0 || selectedIndex >= m_selectedRows.size()) {
        return;
    }

    auto &selected = m_selectedRows[selectedIndex];
    const QString oldResource = selected.binding.resourceId;
    const QString targetResource = selected.binding.resourceId;
    for (auto &candidate : m_availablePorts) {
        if (candidate.binding.resourceId == targetResource && candidate.selectedIndex == selectedIndex) {
            candidate.assigned = false;
            candidate.selectedIndex = -1;
            break;
        }
    }

    selected.assigned = false;
    selected.binding.channel = -1;
    selected.binding.resourceId.clear();

    Logger::log(QStringLiteral("PortReview unassign: component=%1 identifier=%2 resource=%3")
                    .arg(selected.componentRef)
                    .arg(selected.binding.identifier)
                    .arg(oldResource),
                Logger::Level::Info);
}

int PortAllocationReviewDialog::firstUnassignedRowByType(TPSPortType type) const
{
    for (int i = 0; i < m_selectedRows.size(); ++i) {
        if (!m_selectedRows[i].assigned && m_selectedRows[i].binding.type == type) {
            return i;
        }
    }
    return -1;
}

void PortAllocationReviewDialog::refreshAvailableTable()
{
    ui->availableTable->setRowCount(m_availablePorts.size());

    for (int i = 0; i < m_availablePorts.size(); ++i) {
        const auto &candidate = m_availablePorts[i];
        ui->availableTable->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        ui->availableTable->setItem(i, 1, new QTableWidgetItem(deviceKindName(candidate.binding.deviceKind)));
        ui->availableTable->setItem(i, 2, new QTableWidgetItem(QString::number(candidate.binding.channel)));
        ui->availableTable->setItem(i, 3, new QTableWidgetItem(portTypeName(candidate.binding.type)));
        ui->availableTable->setItem(i, 4, new QTableWidgetItem(candidate.assigned
                                                                ? QStringLiteral("已分配")
                                                                : QStringLiteral("可用")));

        auto *button = new QPushButton(QStringLiteral("分配"), ui->availableTable);
        button->setEnabled(!candidate.assigned);
        connect(button, &QPushButton::clicked, this, [this, i]() {
            if (i < 0 || i >= m_availablePorts.size()) {
                return;
            }
            const int target = firstUnassignedRowByType(m_availablePorts[i].binding.type);
            if (target < 0) {
                QMessageBox::information(this,
                                         QStringLiteral("端口分配"),
                                         QStringLiteral("该类型端口需求已满足。"));
                return;
            }
            assignCandidateToSelected(i, target);
            refreshAvailableTable();
            refreshSelectedTable();
            rebuildSummary();
        });

        auto *holder = new QWidget(ui->availableTable);
        auto *layout = new QHBoxLayout(holder);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setAlignment(Qt::AlignCenter);
        layout->addWidget(button);
        ui->availableTable->setCellWidget(i, 5, holder);
        ui->availableTable->setRowHeight(i, 32);
    }
}

void PortAllocationReviewDialog::refreshSelectedTable()
{
    ui->selectedTable->setRowCount(m_selectedRows.size());

    int assignedCount = 0;
    for (int i = 0; i < m_selectedRows.size(); ++i) {
        const auto &selected = m_selectedRows[i];
        assignedCount += selected.assigned ? 1 : 0;

        ui->selectedTable->setItem(i, 0, new QTableWidgetItem(QString::number(i + 1)));
        ui->selectedTable->setItem(i, 1, new QTableWidgetItem(selected.componentRef));
        ui->selectedTable->setItem(i, 2, new QTableWidgetItem(selected.binding.identifier));
        ui->selectedTable->setItem(i, 3, new QTableWidgetItem(portTypeName(selected.binding.type)));
        ui->selectedTable->setItem(i, 4, new QTableWidgetItem(selected.assigned
                                                               ? selected.binding.resourceId
                                                               : QStringLiteral("(未分配)")));

        auto *button = new QPushButton(QStringLiteral("移除"), ui->selectedTable);
        button->setEnabled(selected.assigned);
        connect(button, &QPushButton::clicked, this, [this, i]() {
            unassignSelected(i);
            refreshAvailableTable();
            refreshSelectedTable();
            rebuildSummary();
        });

        auto *holder = new QWidget(ui->selectedTable);
        auto *layout = new QHBoxLayout(holder);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setAlignment(Qt::AlignCenter);
        layout->addWidget(button);
        ui->selectedTable->setCellWidget(i, 5, holder);
        ui->selectedTable->setRowHeight(i, 32);
    }

    ui->selectedCountLabel->setText(QStringLiteral("已分配端口数：%1").arg(assignedCount));
}

QVector<TPSPortBinding> PortAllocationReviewDialog::candidatesForBinding(const TPSPortBinding &binding) const
{
    QVector<TPSPortBinding> candidates;
    auto appendRange = [&](JYDeviceKind kind, int start, int end) {
        for (int channel = start; channel <= end; ++channel) {
            TPSPortBinding option = binding;
            option.deviceKind = kind;
            option.channel = channel;
            option.resourceId = QStringLiteral("%1.CH%2").arg(resourcePrefix(kind)).arg(channel);
            candidates.push_back(option);
        }
    };

    switch (binding.type) {
    case TPSPortType::CurrentOutput:
        appendRange(JYDeviceKind::PXIe5711,
                    PortDefinitions::JY5711Ports::Current_OUTPUT_START,
                    PortDefinitions::JY5711Ports::Current_OUTPUT_END);
        break;
    case TPSPortType::VoltageOutput:
        appendRange(JYDeviceKind::PXIe5711,
                    PortDefinitions::JY5711Ports::Voltage_OUTPUT_START,
                    PortDefinitions::JY5711Ports::Voltage_OUTPUT_END);
        break;
    case TPSPortType::CurrentInput:
        appendRange(JYDeviceKind::PXIe5323,
                    PortDefinitions::JY5323Ports::Current_INPUT_START,
                    PortDefinitions::JY5323Ports::Current_INPUT_END);
        break;
    case TPSPortType::VoltageInput:
        appendRange(JYDeviceKind::PXIe5322,
                    PortDefinitions::JY5322Ports::Voltage_INPUT_START,
                    PortDefinitions::JY5322Ports::Voltage_INPUT_END);
        break;
    case TPSPortType::DmmChannel:
        appendRange(JYDeviceKind::PXIe8902,
                    PortDefinitions::JY8902Ports::DMM_CHANNEL_START,
                    PortDefinitions::JY8902Ports::DMM_CHANNEL_END);
        break;
    }

    return candidates;
}

QString PortAllocationReviewDialog::formatPortCandidate(const TPSPortBinding &binding) const
{
    return QStringLiteral("%1 | %2 | CH%3")
        .arg(portTypeName(binding.type))
        .arg(deviceKindName(binding.deviceKind))
        .arg(binding.channel);
}

void PortAllocationReviewDialog::rebuildSummary()
{
    QMap<TPSPortType, int> demand;
    QMap<TPSPortType, int> assigned;
    for (const auto &selected : m_selectedRows) {
        demand[selected.binding.type] += 1;
        if (selected.assigned) {
            assigned[selected.binding.type] += 1;
        }
    }

    ui->demandTable->setRowCount(demand.size());
    int row = 0;
    for (auto it = demand.begin(); it != demand.end(); ++it, ++row) {
        const int need = it.value();
        const int has = assigned.value(it.key(), 0);
        const QString status = has >= need
            ? QStringLiteral("已满足")
            : QStringLiteral("缺 %1").arg(need - has);

        ui->demandTable->setItem(row, 0, new QTableWidgetItem(portTypeName(it.key())));
        ui->demandTable->setItem(row, 1, new QTableWidgetItem(QString::number(need)));
        ui->demandTable->setItem(row, 2, new QTableWidgetItem(QString::number(has)));
        ui->demandTable->setItem(row, 3, new QTableWidgetItem(status));
        ui->demandTable->setRowHeight(row, 28);
    }

    QStringList summary;
    for (auto it = demand.begin(); it != demand.end(); ++it) {
        const int need = it.value();
        const int has = assigned.value(it.key(), 0);
        summary << QStringLiteral("%1: %2/%3").arg(portTypeName(it.key())).arg(has).arg(need);
    }
    ui->summaryLabel->setText(QStringLiteral("端口需求总览：") + summary.join(QStringLiteral("    ")));
}
