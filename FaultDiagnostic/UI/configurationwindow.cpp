#include "configurationwindow.h"
#include "ui_configurationwindow.h"

#include "../TPS/Manager/tpspluginmanager.h"
#include "../TPS/Manager/tpsbuiltinregistry.h"
#include "../Core/testsequencemanager.h"
#include "../../componenttyperegistry.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSaveFile>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString themeQssPath()
{
    const bool dark = qApp && qApp->property("atsTheme").toString() == QStringLiteral("dark");
    return dark ? QStringLiteral("styles/testsequence_dark.qss")
                : QStringLiteral("styles/testsequence_light.qss");
}

QString resolvePluginDir(const QString &relativePath)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
    candidates << QDir(appDir).filePath(relativePath);

    QDir probe(appDir);
    for (int depth = 0; depth < 8; ++depth) {
        candidates << probe.filePath(relativePath);
        if (!probe.cdUp()) {
            break;
        }
    }

    for (const QString &candidate : candidates) {
        QFileInfo info(candidate);
        if (info.exists() && info.isDir()) {
            return QDir::cleanPath(candidate);
        }
    }

    return QDir::cleanPath(candidates.first());
}

struct AddDialogResult {
    bool accepted = false;
    QString componentRef;
    QString pluginId;
    QMap<QString, QVariant> parameters;
};

QTableWidget *sequenceTable(QWidget *owner)
{
    return owner ? owner->findChild<QTableWidget *>(QStringLiteral("sequenceTable")) : nullptr;
}
}

ConfigurationWindow::ConfigurationWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ConfigurationWindow)
{
    ui->setupUi(this);

    if (auto *table = sequenceTable(this)) {
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({tr("Ref"), tr("Type"), tr("Plugin")});
        table->horizontalHeader()->setStretchLastSection(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(table, &QTableWidget::itemSelectionChanged, this, &ConfigurationWindow::onSelectionChanged);
    }

    loadPlugins();
    loadComponentBindings();
    setupBindingEditorUi();
    rebuildBindingEditorUi();
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

int ConfigurationWindow::currentIndex() const
{
    return m_currentIndex;
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
        QMessageBox::information(this, tr("Sequence"), tr("No TPS plugins loaded."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Sequence Item"));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    auto *formLayout = new QFormLayout();
    auto *refEdit = new QLineEdit(&dialog);
    auto *typeBox = new QComboBox(&dialog);
    typeBox->setEditable(true);
    auto *pluginBox = new QComboBox(&dialog);

    QStringList typeKeys = m_componentTypes;
    typeKeys.sort(Qt::CaseInsensitive);
    for (const QString &key : typeKeys) {
        typeBox->addItem(key);
    }

    pluginBox->addItem(tr("(None)"), QString());
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        pluginBox->addItem(it.value()->displayName(), it.key());
    }

    formLayout->addRow(tr("Component Ref"), refEdit);
    formLayout->addRow(tr("Component Type"), typeBox);
    formLayout->addRow(tr("TPS Plugin"), pluginBox);
    layout->addLayout(formLayout);

    auto *paramScroll = new QScrollArea(&dialog);
    paramScroll->setWidgetResizable(true);
    auto *paramContainer = new QWidget(paramScroll);
    auto *paramLayout = new QFormLayout(paramContainer);
    paramScroll->setWidget(paramContainer);
    layout->addWidget(paramScroll);

    QMap<QString, QWidget *> paramEditors;

    const auto rebuildParams = [&paramEditors, paramLayout, pluginBox, paramContainer, this]() {
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

    const auto applySuggestedPlugin = [this, typeBox, pluginBox]() {
        const QString suggested = suggestedPluginForType(typeBox->currentText());
        if (suggested.isEmpty()) {
            return;
        }
        const int idx = pluginBox->findData(suggested);
        if (idx >= 0) {
            pluginBox->setCurrentIndex(idx);
        }
    };

    applySuggestedPlugin();
    rebuildParams();
    connect(typeBox, &QComboBox::editTextChanged, &dialog, [applySuggestedPlugin](const QString &) { applySuggestedPlugin(); });
    connect(pluginBox, &QComboBox::currentIndexChanged, &dialog, [rebuildParams](int) { rebuildParams(); });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString ref = refEdit->text().trimmed();
    if (ref.isEmpty()) {
        QMessageBox::warning(this, tr("Sequence"), tr("Component Ref cannot be empty."));
        return;
    }

    const QString componentType = typeBox->currentText().trimmed();
    if (componentType.isEmpty()) {
        QMessageBox::warning(this, tr("Sequence"), tr("Component Type cannot be empty."));
        return;
    }

    const QString pluginId = pluginBox->currentData().toString();
    const auto *plugin = m_plugins.value(pluginId, nullptr);
    const auto defs = plugin ? plugin->parameterDefinitions() : QVector<TPSParamDefinition>();
    const auto params = collectParams(defs, paramEditors);

    TestSequenceManager::Item item;
    item.componentRef = ref;
    item.componentType = componentType;
    item.pluginId = pluginId;
    item.parameters = params;
    m_manager->addItem(item);

    const QString key = normalizedComponentType(componentType);
    if (!key.isEmpty()) {
        if (!m_componentTypes.contains(componentType, Qt::CaseInsensitive)) {
            m_componentTypes.push_back(componentType);
        }
        if (pluginId.isEmpty()) {
            m_componentPluginBindings.remove(key);
        } else {
            m_componentPluginBindings.insert(key, pluginId);
        }
        QString error;
        if (!saveComponentBindings(&error)) {
            QMessageBox::warning(this, tr("Sequence"), tr("Failed to save type bindings: %1").arg(error));
        }
        rebuildBindingEditorUi();
    }
}
void ConfigurationWindow::onRemoveItem()
{
    auto *table = sequenceTable(this);
    if (!m_manager || !table) {
        return;
    }
    const int row = table->currentRow();
    if (row < 0) {
        return;
    }
    m_manager->removeItem(row);
    m_currentIndex = -1;
}

void ConfigurationWindow::onImportSequence()
{
    if (!m_manager) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("鍔犺浇娴嬭瘯搴忓垪"), QString(), tr("JSON Files (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!m_manager->loadFromFile(path, &error)) {
        QMessageBox::warning(this, tr("娴嬭瘯搴忓垪"), error);
    }
}

void ConfigurationWindow::onExportSequence()
{
    if (!m_manager) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("瀵煎嚭娴嬭瘯搴忓垪"), QString(), tr("JSON Files (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!m_manager->saveToFile(path, &error)) {
        QMessageBox::warning(this, tr("娴嬭瘯搴忓垪"), error);
    }
}

void ConfigurationWindow::onSelectionChanged()
{
    auto *table = sequenceTable(this);
    if (!table) {
        return;
    }
    m_currentIndex = table->currentRow();
}

void ConfigurationWindow::onStartTest()
{
    auto *table = sequenceTable(this);
    if (!m_manager || !table) {
        return;
    }
    const int row = table->currentRow();
    if (row < 0 || row >= m_manager->count()) {
        return;
    }
    emit startTestRequested(row);
}

void ConfigurationWindow::loadPlugins()
{
    if (m_pluginManager) {
        return;
    }

    m_pluginManager = new TPSPluginManager(this);
    m_pluginManager->setPluginDir(resolvePluginDir(QStringLiteral("tps_plugins")));
    registerDefaultTpsBuiltins(m_pluginManager, m_pluginManager);

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

void ConfigurationWindow::loadComponentBindings()
{
    const ComponentTypeRegistryData registry = ComponentTypeRegistry::load();
    m_componentTypes = registry.types;
    m_componentPluginBindings = registry.bindings;
    if (m_componentTypes.isEmpty()) {
        m_componentTypes = ComponentTypeRegistry::defaultTypes();
    }
}

bool ConfigurationWindow::saveComponentBindings(QString *errorMessage) const
{
    ComponentTypeRegistryData registry;
    registry.types = m_componentTypes;
    registry.bindings = m_componentPluginBindings;
    return ComponentTypeRegistry::save(registry, errorMessage);
}

QString ConfigurationWindow::normalizedComponentType(const QString &value) const
{
    return ComponentTypeRegistry::normalizeTypeKey(value);
}

QString ConfigurationWindow::suggestedPluginForType(const QString &componentType) const
{
    const QString key = normalizedComponentType(componentType);
    if (key.isEmpty()) {
        return {};
    }

    return m_componentPluginBindings.value(key).trimmed();
}

QString ConfigurationWindow::pluginDisplayName(const QString &pluginId) const
{
    const auto *plugin = m_plugins.value(pluginId, nullptr);
    return plugin ? plugin->displayName() : pluginId;
}

void ConfigurationWindow::setupBindingEditorUi()
{
    if (!ui || !ui->rootLayout || m_bindingTable) {
        return;
    }

    auto *container = new QFrame(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *title = new QLabel(tr("Component Type - TPS Plugin Mapping"), container);
    layout->addWidget(title);

    m_bindingTable = new QTableWidget(container);
    m_bindingTable->setColumnCount(2);
    m_bindingTable->setHorizontalHeaderLabels({tr("Component Type"), tr("TPS Plugin")});
    m_bindingTable->horizontalHeader()->setStretchLastSection(true);
    m_bindingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_bindingTable->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_bindingTable);

    auto *toolbar = new QHBoxLayout();
    m_bindingAddButton = new QToolButton(container);
    m_bindingAddButton->setText(tr("Add Type"));
    m_bindingRemoveButton = new QToolButton(container);
    m_bindingRemoveButton->setText(tr("Remove Type"));
    toolbar->addWidget(m_bindingAddButton);
    toolbar->addWidget(m_bindingRemoveButton);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    ui->rootLayout->insertWidget(1, container);

    connect(m_bindingAddButton, &QToolButton::clicked, this, &ConfigurationWindow::onAddBindingType);
    connect(m_bindingRemoveButton, &QToolButton::clicked, this, &ConfigurationWindow::onRemoveBindingType);
    connect(m_bindingTable, &QTableWidget::cellChanged, this, &ConfigurationWindow::onBindingCellChanged);
}

void ConfigurationWindow::rebuildBindingEditorUi()
{
    if (!m_bindingTable) {
        return;
    }

    m_updatingBindingEditor = true;
    m_bindingTable->clearContents();
    m_bindingTable->setRowCount(m_componentTypes.size());

    for (int row = 0; row < m_componentTypes.size(); ++row) {
        const QString typeName = m_componentTypes.at(row);
        auto *typeItem = new QTableWidgetItem(typeName);
        if (isBuiltInTypeName(typeName)) {
            typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        }
        m_bindingTable->setItem(row, 0, typeItem);

        auto *pluginBox = new QComboBox(m_bindingTable);
        pluginBox->addItem(tr("(None)"), QString());
        for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
            pluginBox->addItem(it.value()->displayName(), it.key());
        }
        const QString pluginId = m_componentPluginBindings.value(normalizedComponentType(typeName));
        const int idx = pluginBox->findData(pluginId);
        pluginBox->setCurrentIndex(idx >= 0 ? idx : 0);
        m_bindingTable->setCellWidget(row, 1, pluginBox);
        connect(pluginBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, row](int) {
            onBindingPluginChanged(row);
        });
    }
    m_updatingBindingEditor = false;
}

bool ConfigurationWindow::persistBindingsFromEditor(QString *errorMessage)
{
    if (!m_bindingTable) {
        return saveComponentBindings(errorMessage);
    }

    QStringList nextTypes;
    QMap<QString, QString> nextBindings;
    const QStringList fixedTypes = ComponentTypeRegistry::defaultTypes();

    for (const QString &fixed : fixedTypes) {
        if (!nextTypes.contains(fixed, Qt::CaseInsensitive)) {
            nextTypes.push_back(fixed);
        }
    }

    for (int row = 0; row < m_bindingTable->rowCount(); ++row) {
        auto *typeItem = m_bindingTable->item(row, 0);
        auto *pluginBox = qobject_cast<QComboBox *>(m_bindingTable->cellWidget(row, 1));
        const QString typeName = typeItem ? typeItem->text().trimmed() : QString();
        if (typeName.isEmpty()) {
            continue;
        }
        if (!nextTypes.contains(typeName, Qt::CaseInsensitive)) {
            nextTypes.push_back(typeName);
        }
        const QString pluginId = pluginBox ? pluginBox->currentData().toString().trimmed() : QString();
        const QString key = normalizedComponentType(typeName);
        if (!key.isEmpty() && !pluginId.isEmpty()) {
            nextBindings.insert(key, pluginId);
        }
    }

    m_componentTypes = nextTypes;
    m_componentPluginBindings = nextBindings;
    return saveComponentBindings(errorMessage);
}

bool ConfigurationWindow::isBuiltInTypeName(const QString &typeName) const
{
    return ComponentTypeRegistry::isBuiltInType(typeName);
}

void ConfigurationWindow::onAddBindingType()
{
    if (!m_bindingTable) {
        return;
    }
    const int row = m_bindingTable->rowCount();
    m_bindingTable->insertRow(row);
    m_bindingTable->setItem(row, 0, new QTableWidgetItem());

    auto *pluginBox = new QComboBox(m_bindingTable);
    pluginBox->addItem(tr("(None)"), QString());
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        pluginBox->addItem(it.value()->displayName(), it.key());
    }
    m_bindingTable->setCellWidget(row, 1, pluginBox);
    connect(pluginBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, row](int) {
        onBindingPluginChanged(row);
    });

    m_bindingTable->setCurrentCell(row, 0);
    m_bindingTable->editItem(m_bindingTable->item(row, 0));
}

void ConfigurationWindow::onRemoveBindingType()
{
    if (!m_bindingTable) {
        return;
    }
    const int row = m_bindingTable->currentRow();
    if (row < 0) {
        return;
    }
    auto *typeItem = m_bindingTable->item(row, 0);
    const QString typeName = typeItem ? typeItem->text().trimmed() : QString();
    if (isBuiltInTypeName(typeName)) {
        QMessageBox::information(this,
                                 tr("Bindings"),
                                 tr("Built-in component types cannot be removed."));
        return;
    }

    m_bindingTable->removeRow(row);
    QString error;
    if (!persistBindingsFromEditor(&error)) {
        QMessageBox::warning(this, tr("Bindings"), tr("Failed to save bindings: %1").arg(error));
    }
    rebuildBindingEditorUi();
}

void ConfigurationWindow::onBindingCellChanged(int row, int column)
{
    Q_UNUSED(column);
    if (m_updatingBindingEditor || row < 0 || !m_bindingTable) {
        return;
    }

    auto *typeItem = m_bindingTable->item(row, 0);
    if (!typeItem) {
        return;
    }
    const QString trimmed = typeItem->text().trimmed();
    typeItem->setText(trimmed);
    if (trimmed.isEmpty()) {
        return;
    }

    QString error;
    if (!persistBindingsFromEditor(&error)) {
        QMessageBox::warning(this, tr("Bindings"), tr("Failed to save bindings: %1").arg(error));
    }
    rebuildBindingEditorUi();
}

void ConfigurationWindow::onBindingPluginChanged(int row)
{
    if (m_updatingBindingEditor || row < 0 || !m_bindingTable) {
        return;
    }
    auto *typeItem = m_bindingTable->item(row, 0);
    if (!typeItem || typeItem->text().trimmed().isEmpty()) {
        return;
    }
    QString error;
    if (!persistBindingsFromEditor(&error)) {
        QMessageBox::warning(this, tr("Bindings"), tr("Failed to save bindings: %1").arg(error));
    }
}

void ConfigurationWindow::onManageBindings()
{
    if (m_bindingTable) {
        m_bindingTable->setFocus();
    }
}

void ConfigurationWindow::refreshTable()
{
    auto *table = sequenceTable(this);
    if (!m_manager || !table) {
        return;
    }

    const auto items = m_manager->items();
    table->setRowCount(items.size());

    for (int row = 0; row < items.size(); ++row) {
        const auto &item = items.at(row);
        auto *refItem = new QTableWidgetItem(item.componentRef);
        auto *typeItem = new QTableWidgetItem(item.componentType);
        const QString pluginName = pluginDisplayName(item.pluginId);
        auto *pluginItem = new QTableWidgetItem(pluginName);
        table->setItem(row, 0, refItem);
        table->setItem(row, 1, typeItem);
        table->setItem(row, 2, pluginItem);
    }

    if (m_currentIndex >= 0 && m_currentIndex < items.size()) {
        table->selectRow(m_currentIndex);
    } else {
        m_currentIndex = -1;
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

