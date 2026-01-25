#include "configurationwindow.h"
#include "ui_configurationwindow.h"

#include "../TPS/Plugins/exampletpsplugin.h"
#include "../TPS/Plugins/multitpsplugin.h"
#include "../TPS/Plugins/resistancetpsplugin.h"
#include "../TPS/Manager/tpspluginmanager.h"
#include "../Core/testsequencemanager.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString themeQssPath()
{
    const bool dark = qApp && qApp->property("atsTheme").toString() == QStringLiteral("dark");
    return dark ? QStringLiteral("styles/testsequence_dark.qss")
                : QStringLiteral("styles/testsequence_light.qss");
}

struct AddDialogResult {
    bool accepted = false;
    QString componentRef;
    QString pluginId;
    QMap<QString, QVariant> parameters;
};
}

ConfigurationWindow::ConfigurationWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ConfigurationWindow)
{
    ui->setupUi(this);

    if (ui->sequenceTable) {
        ui->sequenceTable->setColumnCount(2);
        ui->sequenceTable->setHorizontalHeaderLabels({tr("元器件"), tr("插件")});
        ui->sequenceTable->horizontalHeader()->setStretchLastSection(true);
        ui->sequenceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->sequenceTable->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(ui->sequenceTable, &QTableWidget::itemSelectionChanged, this, &ConfigurationWindow::onSelectionChanged);
    }

    if (ui->btnAddItem) {
        connect(ui->btnAddItem, &QToolButton::clicked, this, &ConfigurationWindow::onAddItem);
    }
    if (ui->btnRemoveItem) {
        connect(ui->btnRemoveItem, &QToolButton::clicked, this, &ConfigurationWindow::onRemoveItem);
    }
    if (ui->btnImport) {
        connect(ui->btnImport, &QToolButton::clicked, this, &ConfigurationWindow::onImportSequence);
    }
    if (ui->btnExport) {
        connect(ui->btnExport, &QToolButton::clicked, this, &ConfigurationWindow::onExportSequence);
    }

    loadPlugins();
    setSequenceManager(new TestSequenceManager(this));
    applyThemeQss();
}

ConfigurationWindow::~ConfigurationWindow()
{
    delete ui;
}

void ConfigurationWindow::setSequenceManager(TestSequenceManager *manager)
{
    if (m_manager == manager) {
        return;
    }

    if (m_manager) {
        disconnect(m_manager, nullptr, this, nullptr);
    }

    m_manager = manager ? manager : new TestSequenceManager(this);

    connect(m_manager, &TestSequenceManager::sequenceReset, this, &ConfigurationWindow::refreshTable);
    connect(m_manager, &TestSequenceManager::itemAdded, this, &ConfigurationWindow::refreshTable);
    connect(m_manager, &TestSequenceManager::itemRemoved, this, &ConfigurationWindow::refreshTable);
    connect(m_manager, &TestSequenceManager::itemUpdated, this, &ConfigurationWindow::refreshTable);

    refreshTable();
}

TestSequenceManager *ConfigurationWindow::sequenceManager() const
{
    return m_manager;
}

void ConfigurationWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        applyThemeQss();
    }
}

void ConfigurationWindow::onAddItem()
{
    if (!m_manager || m_plugins.isEmpty()) {
        QMessageBox::information(this, tr("测试序列"), tr("未发现可用的TPS插件。"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("新增被测元器件"));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    auto *formLayout = new QFormLayout();
    auto *refEdit = new QLineEdit(&dialog);
    auto *pluginBox = new QComboBox(&dialog);

    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        pluginBox->addItem(it.value()->displayName(), it.key());
    }

    formLayout->addRow(tr("元器件编号"), refEdit);
    formLayout->addRow(tr("插件"), pluginBox);
    layout->addLayout(formLayout);

    auto *paramScroll = new QScrollArea(&dialog);
    paramScroll->setWidgetResizable(true);
    auto *paramContainer = new QWidget(paramScroll);
    auto *paramLayout = new QFormLayout(paramContainer);
    paramScroll->setWidget(paramContainer);
    layout->addWidget(paramScroll);

    QMap<QString, QWidget *> paramEditors;

    const auto rebuildParams = [&]() {
        qDeleteAll(paramEditors);
        paramEditors.clear();
        while (paramLayout->rowCount() > 0) {
            paramLayout->removeRow(0);
        }

        const QString pluginId = pluginBox->currentData().toString();
        const auto *plugin = m_plugins.value(pluginId, nullptr);
        if (!plugin) {
            return;
        }
        const auto defs = plugin->parameterDefinitions();
        for (const auto &def : defs) {
            QWidget *editor = createParamEditor(def, def.defaultValue, paramContainer);
            paramEditors.insert(def.key, editor);
            paramLayout->addRow(def.label, editor);
        }
    };

    rebuildParams();
    connect(pluginBox, &QComboBox::currentIndexChanged, &dialog, [=](int) { rebuildParams(); });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString ref = refEdit->text().trimmed();
    if (ref.isEmpty()) {
        QMessageBox::warning(this, tr("测试序列"), tr("元器件编号不能为空。"));
        return;
    }

    const QString pluginId = pluginBox->currentData().toString();
    const auto *plugin = m_plugins.value(pluginId, nullptr);
    const auto defs = plugin ? plugin->parameterDefinitions() : QVector<TPSParamDefinition>();
    const auto params = collectParams(defs, paramEditors);

    TestSequenceManager::Item item;
    item.componentRef = ref;
    item.pluginId = pluginId;
    item.parameters = params;
    m_manager->addItem(item);
}

void ConfigurationWindow::onRemoveItem()
{
    if (!m_manager || !ui->sequenceTable) {
        return;
    }
    const int row = ui->sequenceTable->currentRow();
    if (row < 0) {
        return;
    }
    m_manager->removeItem(row);
    clearDetail();
}

void ConfigurationWindow::onImportSequence()
{
    if (!m_manager) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("加载测试序列"), QString(), tr("JSON Files (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!m_manager->loadFromFile(path, &error)) {
        QMessageBox::warning(this, tr("测试序列"), error);
    }
}

void ConfigurationWindow::onExportSequence()
{
    if (!m_manager) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("导出测试序列"), QString(), tr("JSON Files (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!m_manager->saveToFile(path, &error)) {
        QMessageBox::warning(this, tr("测试序列"), error);
    }
}

void ConfigurationWindow::onSelectionChanged()
{
    if (!ui->sequenceTable) {
        return;
    }
    const int row = ui->sequenceTable->currentRow();
    refreshDetail(row);
}

void ConfigurationWindow::loadPlugins()
{
    if (m_pluginManager) {
        return;
    }

    m_pluginManager = new TPSPluginManager(this);
    m_pluginManager->setPluginDir(QStringLiteral("D:/FaultDetect/Program/FaultDetect/ATS/FaultDiagnostic/TPS/Plugins"));

    auto *example = new ExampleTpsPlugin();
    example->setParent(m_pluginManager);
    m_pluginManager->addBuiltin(example);

    auto *resistance = new ResistanceTpsPlugin();
    resistance->setParent(m_pluginManager);
    m_pluginManager->addBuiltin(resistance);

    auto *multi = new MultiSignalTpsPlugin();
    multi->setParent(m_pluginManager);
    m_pluginManager->addBuiltin(multi);

    QString error;
    m_pluginManager->loadAll(&error);

    const auto ids = m_pluginManager->pluginIds();
    for (const auto &id : ids) {
        auto *plugin = m_pluginManager->plugin(id);
        if (plugin) {
            m_plugins.insert(id, plugin);
        }
    }
}

void ConfigurationWindow::refreshTable()
{
    if (!m_manager || !ui->sequenceTable) {
        return;
    }

    const auto items = m_manager->items();
    ui->sequenceTable->setRowCount(items.size());

    for (int row = 0; row < items.size(); ++row) {
        const auto &item = items.at(row);
        auto *refItem = new QTableWidgetItem(item.componentRef);
        const auto *plugin = m_plugins.value(item.pluginId, nullptr);
        const QString pluginName = plugin ? plugin->displayName() : item.pluginId;
        auto *pluginItem = new QTableWidgetItem(pluginName);
        ui->sequenceTable->setItem(row, 0, refItem);
        ui->sequenceTable->setItem(row, 1, pluginItem);
    }

    if (m_currentIndex >= 0 && m_currentIndex < items.size()) {
        ui->sequenceTable->selectRow(m_currentIndex);
        refreshDetail(m_currentIndex);
    } else {
        clearDetail();
    }
}

void ConfigurationWindow::refreshDetail(int index)
{
    if (!m_manager || !ui->labelDetailTitle || !ui->labelPluginName || !ui->labelComponentRef) {
        return;
    }

    if (index < 0 || index >= m_manager->count()) {
        clearDetail();
        return;
    }

    m_currentIndex = index;
    const auto item = m_manager->itemAt(index);
    const auto *plugin = m_plugins.value(item.pluginId, nullptr);

    ui->labelDetailTitle->setText(tr("测试参数"));
    ui->labelComponentRef->setText(item.componentRef);
    ui->labelPluginName->setText(plugin ? plugin->displayName() : item.pluginId);

    if (!ui->paramFormLayout) {
        return;
    }

    m_updatingDetail = true;
    qDeleteAll(m_detailEditors);
    m_detailEditors.clear();
    while (ui->paramFormLayout->rowCount() > 0) {
        ui->paramFormLayout->removeRow(0);
    }

    const auto defs = plugin ? plugin->parameterDefinitions() : QVector<TPSParamDefinition>();
    for (const auto &def : defs) {
        const QVariant value = item.parameters.value(def.key, def.defaultValue);
        QWidget *editor = createParamEditor(def, value, ui->paramContainer);
        m_detailEditors.insert(def.key, editor);
        ui->paramFormLayout->addRow(def.label, editor);

        if (auto *line = qobject_cast<QLineEdit *>(editor)) {
            connect(line, &QLineEdit::editingFinished, this, [this, defs]() {
                if (m_updatingDetail) return;
                const auto params = collectParams(defs, m_detailEditors);
                m_manager->updateParameters(m_currentIndex, params);
            });
        } else if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
            connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, [this, defs](int) {
                if (m_updatingDetail) return;
                const auto params = collectParams(defs, m_detailEditors);
                m_manager->updateParameters(m_currentIndex, params);
            });
        } else if (auto *dspin = qobject_cast<QDoubleSpinBox *>(editor)) {
            connect(dspin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, defs](double) {
                if (m_updatingDetail) return;
                const auto params = collectParams(defs, m_detailEditors);
                m_manager->updateParameters(m_currentIndex, params);
            });
        } else if (auto *check = qobject_cast<QCheckBox *>(editor)) {
            connect(check, &QCheckBox::toggled, this, [this, defs](bool) {
                if (m_updatingDetail) return;
                const auto params = collectParams(defs, m_detailEditors);
                m_manager->updateParameters(m_currentIndex, params);
            });
        } else if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, defs](int) {
                if (m_updatingDetail) return;
                const auto params = collectParams(defs, m_detailEditors);
                m_manager->updateParameters(m_currentIndex, params);
            });
        }
    }

    m_updatingDetail = false;
}

void ConfigurationWindow::clearDetail()
{
    m_currentIndex = -1;
    if (ui->labelDetailTitle) ui->labelDetailTitle->setText(tr("测试参数"));
    if (ui->labelComponentRef) ui->labelComponentRef->setText(tr("-"));
    if (ui->labelPluginName) ui->labelPluginName->setText(tr("-"));
    if (ui->paramFormLayout) {
        qDeleteAll(m_detailEditors);
        m_detailEditors.clear();
        while (ui->paramFormLayout->rowCount() > 0) {
            ui->paramFormLayout->removeRow(0);
        }
    }
}

QWidget *ConfigurationWindow::createParamEditor(const TPSParamDefinition &def, const QVariant &value, QWidget *parent)
{
    QWidget *owner = parent ? parent : this;
    switch (def.type) {
    case TPSParamType::Integer: {
        auto *spin = new QSpinBox(owner);
        if (def.minValue.isValid()) spin->setMinimum(def.minValue.toInt());
        if (def.maxValue.isValid()) spin->setMaximum(def.maxValue.toInt());
        spin->setSingleStep(def.stepValue.isValid() ? def.stepValue.toInt() : 1);
        spin->setValue(value.isValid() ? value.toInt() : def.defaultValue.toInt());
        return spin;
    }
    case TPSParamType::Double: {
        auto *spin = new QDoubleSpinBox(owner);
        spin->setDecimals(6);
        if (def.minValue.isValid()) spin->setMinimum(def.minValue.toDouble());
        if (def.maxValue.isValid()) spin->setMaximum(def.maxValue.toDouble());
        spin->setSingleStep(def.stepValue.isValid() ? def.stepValue.toDouble() : 0.1);
        spin->setValue(value.isValid() ? value.toDouble() : def.defaultValue.toDouble());
        return spin;
    }
    case TPSParamType::Boolean: {
        auto *check = new QCheckBox(owner);
        check->setChecked(value.isValid() ? value.toBool() : def.defaultValue.toBool());
        return check;
    }
    case TPSParamType::Enum: {
        auto *combo = new QComboBox(owner);
        combo->addItems(def.enumOptions);
        const QString current = value.isValid() ? value.toString() : def.defaultValue.toString();
        const int idx = combo->findText(current);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
        return combo;
    }
    case TPSParamType::String:
    default: {
        auto *edit = new QLineEdit(owner);
        edit->setText(value.isValid() ? value.toString() : def.defaultValue.toString());
        return edit;
    }
    }
}

QVariant ConfigurationWindow::readParamValue(const TPSParamDefinition &def, QWidget *editor) const
{
    if (!editor) {
        return {};
    }
    switch (def.type) {
    case TPSParamType::Integer:
        return qobject_cast<QSpinBox *>(editor)->value();
    case TPSParamType::Double:
        return qobject_cast<QDoubleSpinBox *>(editor)->value();
    case TPSParamType::Boolean:
        return qobject_cast<QCheckBox *>(editor)->isChecked();
    case TPSParamType::Enum:
        return qobject_cast<QComboBox *>(editor)->currentText();
    case TPSParamType::String:
    default:
        return qobject_cast<QLineEdit *>(editor)->text();
    }
}

QMap<QString, QVariant> ConfigurationWindow::collectParams(const QVector<TPSParamDefinition> &defs,
                                                           const QMap<QString, QWidget *> &editors) const
{
    QMap<QString, QVariant> params;
    for (const auto &def : defs) {
        params.insert(def.key, readParamValue(def, editors.value(def.key)));
    }
    return params;
}

void ConfigurationWindow::applyThemeQss()
{
    if (m_applyingQss) {
        return;
    }
    m_applyingQss = true;
    QFile file(themeQssPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString qss = QString::fromUtf8(file.readAll());
        if (!qss.isEmpty()) {
            setStyleSheet(qss);
        }
    }
    m_applyingQss = false;
}
