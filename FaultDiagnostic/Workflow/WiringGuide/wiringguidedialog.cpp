#include "wiringguidedialog.h"
#include "ui_wiringguidedialog.h"

#include "../TemperatureRoi/temperatureroiselectdialog.h"
#include "../../../tool/lebalitemmanager.h"

#include <QMessageBox>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
int resolveLabelIdFromTarget(const QString &target)
{
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) {
        return -1;
    }

    bool ok = false;
    const int direct = trimmed.toInt(&ok);
    if (ok) {
        return direct;
    }

    const QRegularExpression re(QStringLiteral("(\\d+)$"));
    const QRegularExpressionMatch match = re.match(trimmed);
    if (!match.hasMatch()) {
        return -1;
    }
    return match.captured(1).toInt(&ok);
}
}

WiringGuideDialog::WiringGuideDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::WiringGuideDialog)
{
    ui->setupUi(this);
    ui->stepList->setSelectionMode(QAbstractItemView::NoSelection);

    auto *containerLayout = new QVBoxLayout(ui->labelManagerContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    m_labelManager = new LebalItemManager(ui->labelManagerContainer);
    containerLayout->addWidget(m_labelManager);

    ui->splitter->setStretchFactor(0, 0);
    ui->splitter->setStretchFactor(1, 1);
    ui->splitter->setSizes({380, 780});

    connect(ui->nextButton, &QPushButton::clicked, this, [this]() { onNextStep(); });
    connect(ui->selectRoiButton, &QPushButton::clicked, this, [this]() { onSelectRoi(); });
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &WiringGuideDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &WiringGuideDialog::reject);
    refreshRoiStatus();
}

WiringGuideDialog::~WiringGuideDialog()
{
    delete ui;
}

void WiringGuideDialog::setItems(const QVector<TaskWiringGuideItem> &items)
{
    m_steps.clear();
    m_currentStep = -1;
    m_selectedRoi = QRectF();
    m_selectedRois.clear();
    m_hasSelectedRoi = false;

    for (const auto &item : items) {
        const QString component = item.componentRef.trimmed().isEmpty()
            ? QStringLiteral("(未命名元件)")
            : item.componentRef.trimmed();
        const QString defaultFocusTarget = item.componentRef.trimmed();

        int step = 1;
        for (int i = 0; i < item.wiringSteps.size(); ++i) {
            const QString instruction = item.wiringSteps.at(i);
            GuidedStep guided;
            guided.text = QStringLiteral("【%1】%2. %3").arg(component).arg(step++).arg(instruction);
            guided.focusTarget = (i >= 0 && i < item.wiringFocusTargets.size())
                ? item.wiringFocusTargets.at(i).trimmed()
                : defaultFocusTarget;
            if (guided.focusTarget.isEmpty()) {
                guided.focusTarget = defaultFocusTarget;
            }
            guided.isRoiStep = false;
            guided.status = StepStatus::NotStarted;
            m_steps.push_back(guided);
        }

        QStringList roiSteps = item.roiSteps;
        if (roiSteps.isEmpty() && !item.temperatureGuide.trimmed().isEmpty()) {
            roiSteps.push_back(item.temperatureGuide.trimmed());
        }

        for (int i = 0; i < roiSteps.size(); ++i) {
            GuidedStep guided;
            guided.text = QStringLiteral("【%1】测温：%2").arg(component, roiSteps.at(i).trimmed());
            guided.focusTarget = (i >= 0 && i < item.roiFocusTargets.size())
                ? item.roiFocusTargets.at(i).trimmed()
                : defaultFocusTarget;
            if (guided.focusTarget.isEmpty()) {
                guided.focusTarget = defaultFocusTarget;
            }
            guided.isRoiStep = true;
            guided.status = StepStatus::NotStarted;
            m_steps.push_back(guided);
        }
    }

    if (!m_steps.isEmpty()) {
        m_currentStep = 0;
        m_steps[0].status = StepStatus::InProgress;
    }

    refreshStepList();
    refreshProgress();
    refreshRoiStatus();
    focusOnCurrentStep();
}

QRectF WiringGuideDialog::selectedRoi() const
{
    return m_selectedRoi;
}

QVector<QRectF> WiringGuideDialog::selectedRois() const
{
    return m_selectedRois;
}

bool WiringGuideDialog::hasSelectedRoi() const
{
    return m_hasSelectedRoi;
}

void WiringGuideDialog::setGuidanceImage(const QImage &image)
{
    if (m_labelManager) {
        m_labelManager->setImage(image);
    }
}

void WiringGuideDialog::setGuidanceLabels(const QList<CompLabel> &labels, bool clearExisting)
{
    if (m_labelManager) {
        m_labelManager->setLabels(labels, clearExisting);
    }
}

void WiringGuideDialog::accept()
{
    if (!allCompleted()) {
        QMessageBox::information(this,
                                 QStringLiteral("接线引导"),
                                 QStringLiteral("仍有步骤未完成，请继续点击“下一步”完成全部引导。"));
        return;
    }

    QDialog::accept();
}

void WiringGuideDialog::onNextStep()
{
    if (m_currentStep < 0 || m_currentStep >= m_steps.size()) {
        return;
    }

    if (m_steps[m_currentStep].isRoiStep && !m_hasSelectedRoi) {
        QMessageBox::information(this,
                                 QStringLiteral("测温框选"),
                                 QStringLiteral("当前步骤需要先完成测温区域框选，请点击“框选测温区域”。"));
        return;
    }

    m_steps[m_currentStep].status = StepStatus::Completed;
    ++m_currentStep;
    if (m_currentStep >= 0 && m_currentStep < m_steps.size()) {
        m_steps[m_currentStep].status = StepStatus::InProgress;
    }

    refreshStepList();
    refreshProgress();
    focusOnCurrentStep();
}

void WiringGuideDialog::onSelectRoi()
{
    TemperatureRoiSelectDialog roiDialog(this);
    if (roiDialog.exec() != QDialog::Accepted || !roiDialog.hasSelectedRoi()) {
        return;
    }

    m_selectedRois = roiDialog.selectedRois();
    m_hasSelectedRoi = !m_selectedRois.isEmpty();
    m_selectedRoi = m_hasSelectedRoi ? m_selectedRois.first() : QRectF();
    refreshRoiStatus();
}

void WiringGuideDialog::refreshStepList()
{
    if (!ui || !ui->stepList) {
        return;
    }

    ui->stepList->clear();
    for (int i = 0; i < m_steps.size(); ++i) {
        const auto &step = m_steps[i];
        QString prefix;
        QColor bg;
        QColor fg;
        switch (step.status) {
        case StepStatus::NotStarted:
            prefix = QStringLiteral("○ ");
            bg = QColor(QStringLiteral("#f5f5f5"));
            fg = QColor(QStringLiteral("#999999"));
            break;
        case StepStatus::InProgress:
            prefix = QStringLiteral("▶ ");
            bg = QColor(QStringLiteral("#e3f2fd"));
            fg = QColor(QStringLiteral("#1565c0"));
            break;
        case StepStatus::Completed:
            prefix = QStringLiteral("✅ ");
            bg = QColor(QStringLiteral("#e8f5e9"));
            fg = QColor(QStringLiteral("#2e7d32"));
            break;
        }

        auto *item = new QListWidgetItem(prefix + step.text, ui->stepList);
        item->setBackground(bg);
        item->setForeground(fg);
    }

    if (m_currentStep >= 0 && m_currentStep < ui->stepList->count()) {
        ui->stepList->scrollToItem(ui->stepList->item(m_currentStep), QAbstractItemView::EnsureVisible);
    }
}

void WiringGuideDialog::refreshProgress()
{
    int completed = 0;
    for (const auto &step : m_steps) {
        if (step.status == StepStatus::Completed) {
            ++completed;
        }
    }

    if (ui && ui->progressLabel) {
        ui->progressLabel->setText(QStringLiteral("进度: %1 / %2").arg(completed).arg(m_steps.size()));
    }

    if (ui && ui->nextButton) {
        const bool done = allCompleted();
        ui->nextButton->setEnabled(!done && m_currentStep >= 0 && m_currentStep < m_steps.size());
        ui->nextButton->setText(done ? QStringLiteral("全部完成")
                                     : QStringLiteral("下一步 (%1/%2)").arg(qMax(1, m_currentStep + 1)).arg(m_steps.size()));
    }
}

void WiringGuideDialog::refreshRoiStatus()
{
    if (!ui || !ui->roiStatusLabel) {
        return;
    }

    if (!m_hasSelectedRoi) {
        ui->roiStatusLabel->setText(QStringLiteral("状态：未框选"));
        return;
    }

    const QRectF first = m_selectedRois.isEmpty() ? m_selectedRoi : m_selectedRois.first();
    ui->roiStatusLabel->setText(
        QStringLiteral("状态：已框选 %1 个，首框(%2, %3, %4, %5)")
            .arg(m_selectedRois.isEmpty() ? 1 : m_selectedRois.size())
            .arg(first.x(), 0, 'f', 1)
            .arg(first.y(), 0, 'f', 1)
            .arg(first.width(), 0, 'f', 1)
            .arg(first.height(), 0, 'f', 1));
}

bool WiringGuideDialog::allCompleted() const
{
    if (m_steps.isEmpty()) {
        return true;
    }

    for (const auto &step : m_steps) {
        if (step.status != StepStatus::Completed) {
            return false;
        }
    }
    return true;
}

void WiringGuideDialog::focusOnCurrentStep()
{
    if (!m_labelManager || m_currentStep < 0 || m_currentStep >= m_steps.size()) {
        return;
    }

    const QString target = m_steps[m_currentStep].focusTarget.trimmed();
    if (target.isEmpty()) {
        return;
    }

    const int id = resolveLabelIdFromTarget(target);
    if (id >= 0 && m_labelManager->centerZoomToLabelId(id)) {
        return;
    }

    m_labelManager->centerZoomToComponent(target);
}
