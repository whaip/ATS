#include "lebalitemmanager.h"
#include "ui_lebalitemmanager.h"

#include "labelediting.h"
#include "../tpsparamservice.h"
#include "../componenttyperegistry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QByteArray encodeNotes(const QByteArray &notes)
{
    return notes.toBase64();
}

QByteArray decodeNotes(const QString &notes)
{
    if (notes.isEmpty()) {
        return {};
    }
    return QByteArray::fromBase64(notes.toUtf8());
}

QString componentTypeNameByClassId(int cls)
{
    const ComponentTypeRegistryData registry = ComponentTypeRegistry::load();
    return ComponentTypeRegistry::typeNameFromClassId(cls, registry);
}

QWidget *createEditorForParam(const TpsParamDefinitionLite &def,
                              const QVariant &value,
                              const QList<int> &anchorIds,
                              QWidget *parent)
{
    const QString keyLower = def.key.toLower();
    const QString labelLower = def.label.toLower();
    const bool isAnchorField = keyLower.contains(QStringLiteral("anchor")) || labelLower.contains(QStringLiteral("锚点"));

    if (isAnchorField && def.type.compare(QStringLiteral("Integer"), Qt::CaseInsensitive) == 0) {
        auto *combo = new QComboBox(parent);
        combo->addItem(QStringLiteral("-1"), -1);
        for (int id : anchorIds) {
            combo->addItem(QString::number(id), id);
        }
        const int currentValue = value.isValid() ? value.toInt() : def.defaultValue.toInt();
        const int index = combo->findData(currentValue);
        combo->setCurrentIndex(index >= 0 ? index : 0);
        return combo;
    }

    if (def.type.compare(QStringLiteral("Integer"), Qt::CaseInsensitive) == 0) {
        auto *spin = new QSpinBox(parent);
        spin->setRange(def.minValue.isValid() ? def.minValue.toInt() : -1000000,
                       def.maxValue.isValid() ? def.maxValue.toInt() : 1000000);
        spin->setSingleStep(def.stepValue.isValid() ? std::max(1, def.stepValue.toInt()) : 1);
        spin->setValue(value.isValid() ? value.toInt() : def.defaultValue.toInt());
        return spin;
    }

    if (def.type.compare(QStringLiteral("Double"), Qt::CaseInsensitive) == 0) {
        auto *spin = new QDoubleSpinBox(parent);
        spin->setDecimals(6);
        spin->setRange(def.minValue.isValid() ? def.minValue.toDouble() : -1e9,
                       def.maxValue.isValid() ? def.maxValue.toDouble() : 1e9);
        spin->setSingleStep(def.stepValue.isValid() ? def.stepValue.toDouble() : 0.1);
        spin->setValue(value.isValid() ? value.toDouble() : def.defaultValue.toDouble());
        return spin;
    }

    if (def.type.compare(QStringLiteral("Boolean"), Qt::CaseInsensitive) == 0) {
        auto *check = new QCheckBox(parent);
        check->setChecked(value.isValid() ? value.toBool() : def.defaultValue.toBool());
        return check;
    }

    if (def.type.compare(QStringLiteral("Enum"), Qt::CaseInsensitive) == 0) {
        auto *combo = new QComboBox(parent);
        combo->addItems(def.enumOptions);
        const QString currentText = value.isValid() ? value.toString() : def.defaultValue.toString();
        int index = combo->findText(currentText);
        combo->setCurrentIndex(index >= 0 ? index : 0);
        return combo;
    }

    auto *edit = new QLineEdit(parent);
    edit->setText(value.isValid() ? value.toString() : def.defaultValue.toString());
    return edit;
}

QVariant readEditorValue(QWidget *editor)
{
    if (auto *spin = qobject_cast<QSpinBox *>(editor)) {
        return spin->value();
    }
    if (auto *doubleSpin = qobject_cast<QDoubleSpinBox *>(editor)) {
        return doubleSpin->value();
    }
    if (auto *check = qobject_cast<QCheckBox *>(editor)) {
        return check->isChecked();
    }
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        if (combo->currentData().isValid()) {
            return combo->currentData();
        }
        return combo->currentText();
    }
    if (auto *line = qobject_cast<QLineEdit *>(editor)) {
        return line->text();
    }
    return {};
}

QString resolveTpsPluginSourceDir(const QString &preferredPath, QStringList *triedPaths = nullptr)
{
    QStringList candidates;
    if (!preferredPath.trimmed().isEmpty()) {
        candidates << QDir::cleanPath(preferredPath);
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << QDir(appDir).filePath(QStringLiteral("tps_plugins"));

    QDir probe(appDir);
    for (int depth = 0; depth < 10; ++depth) {
        candidates << probe.filePath(QStringLiteral("FaultDiagnostic/TPS/Plugins"));
        if (!probe.cdUp()) {
            break;
        }
    }

    QStringList deduplicated;
    deduplicated.reserve(candidates.size());
    for (const QString &path : candidates) {
        const QString clean = QDir::cleanPath(path);
        if (!deduplicated.contains(clean)) {
            deduplicated.push_back(clean);
        }
    }

    for (const QString &path : deduplicated) {
        if (QDir(path).exists()) {
            if (triedPaths) {
                *triedPaths = deduplicated;
            }
            return path;
        }
    }

    if (triedPaths) {
        *triedPaths = deduplicated;
    }
    return {};
}

QString resolveTpsParamDatabasePath(const QString &preferredPath)
{
    QStringList candidates;
    if (!preferredPath.trimmed().isEmpty()) {
        candidates << QDir::cleanPath(preferredPath);
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << QDir(appDir).filePath(QStringLiteral("board_db/i_params_db.json"));

    QDir probe(appDir);
    for (int depth = 0; depth < 10; ++depth) {
        candidates << probe.filePath(QStringLiteral("board_db/i_params_db.json"));
        if (!probe.cdUp()) {
            break;
        }
    }

    candidates << QDir::cleanPath(QDir(appDir).filePath(QStringLiteral("../../i_params_db.json")));

    QStringList deduplicated;
    deduplicated.reserve(candidates.size());
    for (const QString &path : candidates) {
        const QString clean = QDir::cleanPath(path);
        if (!deduplicated.contains(clean)) {
            deduplicated.push_back(clean);
        }
    }

    for (const QString &path : deduplicated) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }

    return deduplicated.isEmpty() ? QString() : deduplicated.first();
}
}

LebalItemManager::LebalItemManager(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LebalItemManager)
{
    ui->setupUi(this);

    m_pluginSourceDir = resolveTpsPluginSourceDir(QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../FaultDiagnostic/TPS/Plugins"))));
    m_tpsParameterDbPath = resolveTpsParamDatabasePath(QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../i_params_db.json"))));

    connect(ui->btnImport, &QPushButton::clicked, this, &LebalItemManager::importLabels);
    connect(ui->btnExport, &QPushButton::clicked, this, &LebalItemManager::exportLabels);
    connect(ui->btnAdd, &QPushButton::clicked, this, &LebalItemManager::addLabel);
    connect(ui->btnDelete, &QPushButton::clicked, this, &LebalItemManager::deleteSelected);
    connect(ui->btnDiagnose, &QPushButton::clicked, this, &LebalItemManager::diagnoseSelected);

    if (ui->leftPanel) {
        ui->leftPanel->hide();
    }
    if (ui->labelHint) {
        ui->labelHint->hide();
    }

    if (ui->graphicsContainer) {
        if (!ui->graphicsContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->graphicsContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }

        std::vector<Label> emptyLabels;
        m_labelEditingWindow = new LabelEditingWindow(ui->graphicsContainer, QImage(), emptyLabels, emptyLabels, m_deleteIds);
        ui->graphicsContainer->layout()->addWidget(m_labelEditingWindow);

        connect(m_labelEditingWindow, &LabelEditingWindow::labelAdded, this, [this](const Label &) {
            emitDiagnoseAll();
            emitLabelsChanged();
        });
        connect(m_labelEditingWindow, &LabelEditingWindow::labelUpdated, this, [this](const Label &) {
            emitDiagnoseAll();
            emitLabelsChanged();
        });
        connect(m_labelEditingWindow, &LabelEditingWindow::labelDeleted, this, [this](int) {
            emitDiagnoseAll();
            emitLabelsChanged();
        });
        connect(m_labelEditingWindow, &LabelEditingWindow::parameterConfigRequested, this,
                [this](const Label &label, const QList<int> &anchorIds) {
                    showTpsParameterDialog(label, anchorIds);
                });
        connect(m_labelEditingWindow, &LabelEditingWindow::selectionChanged, this,
                [this]() {
                    emitSelectedLabelsChanged();
                });
    }
}

LebalItemManager::~LebalItemManager()
{
    delete m_tpsParamService;
    m_tpsParamService = nullptr;
    delete ui;
}

void LebalItemManager::setImage(const QImage &image)
{
    m_currentImage = image;
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing) {
        return;
    }
    m_labelEditingWindow->labelEditing->loadImage(image);
}

bool LebalItemManager::centerZoomToComponent(const QString &componentId)
{
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing || componentId.trimmed().isEmpty()) {
        return false;
    }

    std::vector<Label> labels;
    m_labelEditingWindow->labelEditing->getAllLabelItemInfo(labels);
    const QString key = componentId.trimmed();
    for (const auto &info : labels) {
        if (info.position_number == key || info.label == key || QString::number(info.id) == key) {
            m_labelEditingWindow->labelEditing->triggerTableRowClickById(info.id);
            return true;
        }
    }
    return false;
}

bool LebalItemManager::centerZoomToLabelId(int labelId)
{
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing || labelId < 0) {
        return false;
    }

    m_labelEditingWindow->labelEditing->triggerTableRowClickById(labelId);
    return true;
}

void LebalItemManager::setCurrentBoardId(const QString &boardId)
{
    m_currentBoardId = boardId.trimmed();
}

void LebalItemManager::setTpsParameterDatabasePath(const QString &databasePath)
{
    if (databasePath.trimmed().isEmpty()) {
        return;
    }
    m_tpsParameterDbPath = resolveTpsParamDatabasePath(databasePath);
    if (m_tpsParamService) {
        m_tpsParamService->setDatabasePath(m_tpsParameterDbPath);
    }
}

void LebalItemManager::setLabels(const QList<CompLabel> &labels, bool clearExisting)
{
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing) {
        return;
    }

    std::vector<Label> data;
    data.reserve(static_cast<size_t>(labels.size()));
    for (const auto &label : labels) {
        data.push_back(label);
    }

    if (clearExisting) {
        m_deleteIds.clear();
    }

    m_labelEditingWindow->labelEditing->setLabels(data);

    if (clearExisting) {
        emitDiagnoseAll();
    }
}

QList<CompLabel> LebalItemManager::currentLabels() const
{
    QList<CompLabel> labels;
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing) {
        return labels;
    }

    std::vector<Label> allLabels;
    m_labelEditingWindow->labelEditing->getAllLabelItemInfo(allLabels);
    labels.reserve(static_cast<int>(allLabels.size()));
    for (const auto &label : allLabels) {
        labels.push_back(label);
    }
    return labels;
}

QList<CompLabel> LebalItemManager::selectedLabels() const
{
    QList<CompLabel> labels;
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing) {
        return labels;
    }

    const std::vector<Label> selected = m_labelEditingWindow->labelEditing->getSelectedLabelItemInfos();
    labels.reserve(static_cast<int>(selected.size()));
    for (const auto &label : selected) {
        labels.push_back(label);
    }
    return labels;
}

void LebalItemManager::importLabels()
{
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(this, tr("Import Labels"), QString(), tr("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Import Labels"), tr("Failed to open file."));
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        QMessageBox::warning(this, tr("Import Labels"), tr("Invalid JSON format."));
        return;
    }

    const auto reply = QMessageBox::question(this, tr("Import Labels"), tr("Replace existing labels?"));
    if (reply != QMessageBox::Yes) {
        return;
    }

    QList<CompLabel> labels;
    const QJsonArray array = doc.array();
    for (const auto &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        const int id = obj.value("id").toInt(0);
        const double x = obj.value("x").toDouble(0.0);
        const double y = obj.value("y").toDouble(0.0);
        const double w = obj.value("w").toDouble(120.0);
        const double h = obj.value("h").toDouble(80.0);
        const int cls = obj.value("cls").toInt(0);
        const double conf = obj.value("confidence").toDouble(0.0);
        const QString label = obj.value("label").toString();
        const QString position = obj.value("position_number").toString();
        const QByteArray notes = decodeNotes(obj.value("notes").toString());

        labels.append(CompLabel(id, x, y, w, h, cls, conf, label, position, notes));
    }

    setLabels(labels, true);
}

void LebalItemManager::exportLabels()
{
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export Labels"), QString(), tr("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    std::vector<Label> exportLabels = m_labelEditingWindow->labelEditing->getSelectedLabelItemInfos();
    if (exportLabels.empty()) {
        m_labelEditingWindow->labelEditing->getAllLabelItemInfo(exportLabels);
    }

    std::sort(exportLabels.begin(), exportLabels.end(), [](const Label &a, const Label &b) {
        return a.id < b.id;
    });

    QJsonArray array;
    for (const auto &info : exportLabels) {
        QJsonObject obj;
        obj.insert("id", info.id);
        obj.insert("x", info.x);
        obj.insert("y", info.y);
        obj.insert("w", info.w);
        obj.insert("h", info.h);
        obj.insert("cls", info.cls);
        obj.insert("confidence", info.confidence);
        obj.insert("label", info.label);
        obj.insert("position_number", info.position_number);
        obj.insert("notes", QString::fromUtf8(encodeNotes(info.notes)));
        array.append(obj);
    }

    QJsonDocument doc(array);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export Labels"), tr("Failed to write file."));
        return;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
}

void LebalItemManager::addLabel()
{
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing) {
        return;
    }
    m_labelEditingWindow->labelEditing->on_createRectButton_clicked();
}

void LebalItemManager::deleteSelected()
{
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing) {
        return;
    }
    m_labelEditingWindow->labelEditing->on_deleteButton_clicked();
}

void LebalItemManager::diagnoseSelected()
{
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing) {
        return;
    }

    const std::vector<Label> selected = m_labelEditingWindow->labelEditing->getSelectedLabelItemInfos();
    if (selected.empty()) {
        return;
    }

    QList<CompLabel> labels;
    labels.reserve(static_cast<int>(selected.size()));
    for (const auto &label : selected) {
        labels.push_back(label);
    }
    emit diagnoseRequested(labels);
}

void LebalItemManager::handleSceneSelectionChanged()
{
}

void LebalItemManager::handleListSelectionChanged()
{
}

void LebalItemManager::handleListItemActivated(QListWidgetItem *item)
{
    Q_UNUSED(item);
}

void LebalItemManager::showContextMenu(const QPoint &pos)
{
    Q_UNUSED(pos);
}

void LebalItemManager::emitDiagnoseAll()
{
    if (!m_labelEditingWindow || !m_labelEditingWindow->labelEditing) {
        return;
    }

    std::vector<Label> allLabels;
    m_labelEditingWindow->labelEditing->getAllLabelItemInfo(allLabels);

    QList<CompLabel> labels;
    labels.reserve(static_cast<int>(allLabels.size()));
    for (const auto &label : allLabels) {
        labels.push_back(label);
    }
    emit diagnoseRequested(labels);
}

void LebalItemManager::emitLabelsChanged()
{
    emit labelsChanged(currentLabels());
}

void LebalItemManager::emitSelectedLabelsChanged()
{
    emit selectedLabelsChanged(selectedLabels());
}

bool LebalItemManager::ensureTpsParamServiceLoaded()
{
    if (!m_tpsParamService) {
        m_tpsParamService = new TpsParamService();
    }

    QStringList triedPaths;
    const QString resolvedDir = resolveTpsPluginSourceDir(m_pluginSourceDir, &triedPaths);
    if (resolvedDir.isEmpty()) {
        QMessageBox::warning(this,
                             tr("参数设置"),
                             tr("加载TPS插件失败: 未找到插件目录。\n候选路径:\n%1")
                                 .arg(triedPaths.join(QStringLiteral("\n"))));
        return false;
    }
    m_pluginSourceDir = resolvedDir;

    m_tpsParamService->setPluginSourceDir(m_pluginSourceDir);
    m_tpsParamService->setDatabasePath(m_tpsParameterDbPath);

    QString errorMessage;
    if (!m_tpsParamService->loadPlugins(&errorMessage)) {
        QMessageBox::warning(this,
                             tr("参数设置"),
                             tr("加载TPS插件失败: %1\n当前目录: %2")
                                 .arg(errorMessage)
                                 .arg(m_pluginSourceDir));
        return false;
    }

    return true;
}

void LebalItemManager::showTpsParameterDialog(const CompLabel &label, const QList<int> &anchorIds)
{
    if (m_currentBoardId.isEmpty()) {
        QMessageBox::warning(this, tr("参数设置"), tr("当前板卡ID为空，无法保存参数。"));
        return;
    }

    if (!ensureTpsParamServiceLoaded() || !m_tpsParamService) {
        return;
    }

    const QString componentType = componentTypeNameByClassId(label.cls);
    const TpsPluginSpecLite plugin = m_tpsParamService->pluginForComponentType(componentType);
    if (plugin.pluginId.isEmpty()) {
        QMessageBox::information(this,
                                 tr("参数设置"),
                                 tr("未找到元器件类型 %1 对应的TPS插件。\n请在 TPS/Plugins 目录下提供符合标准格式的插件。")
                                     .arg(componentType));
        return;
    }

    if (plugin.parameters.isEmpty()) {
        QMessageBox::warning(this,
                             tr("参数设置"),
                             tr("插件 %1 未解析到参数定义，无法进行参数设置。\n来源文件: %2")
                                 .arg(plugin.pluginId)
                                 .arg(plugin.sourceFile));
        return;
    }

    QMap<QString, QVariant> values = m_tpsParamService->loadComponentParams(m_currentBoardId, label.id, plugin.pluginId);
    for (const auto &param : plugin.parameters) {
        if (!values.contains(param.key)) {
            values.insert(param.key, param.defaultValue);
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("参数设置 - %1(ID:%2)").arg(componentType).arg(label.id));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    QMap<QString, QWidget *> editors;
    for (const auto &param : plugin.parameters) {
        QWidget *editor = createEditorForParam(param, values.value(param.key), anchorIds, &dialog);
        editors.insert(param.key, editor);
        form->addRow(param.label, editor);
    }
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QMap<QString, QVariant> params;
    for (auto it = editors.begin(); it != editors.end(); ++it) {
        params.insert(it.key(), readEditorValue(it.value()));
    }

    QString errorMessage;
    if (!m_tpsParamService->saveComponentParams(m_currentBoardId,
                                                label.id,
                                                plugin.pluginId,
                                                componentType,
                                                params,
                                                &errorMessage)) {
        QMessageBox::warning(this, tr("参数设置"), tr("参数保存失败: %1").arg(errorMessage));
        return;
    }

    QMessageBox::information(this,
                             tr("参数设置"),
                             tr("参数已保存\n板卡ID: %1\n元器件ID: %2\n插件: %3")
                                 .arg(m_currentBoardId)
                                 .arg(label.id)
                                 .arg(plugin.pluginId));
}
