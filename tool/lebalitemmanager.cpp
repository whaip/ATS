#include "lebalitemmanager.h"
#include "ui_lebalitemmanager.h"

#include "labelediting.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
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
}

LebalItemManager::LebalItemManager(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LebalItemManager)
{
    ui->setupUi(this);

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
        });
        connect(m_labelEditingWindow, &LabelEditingWindow::labelUpdated, this, [this](const Label &) {
            emitDiagnoseAll();
        });
        connect(m_labelEditingWindow, &LabelEditingWindow::labelDeleted, this, [this](int) {
            emitDiagnoseAll();
        });
    }
}

LebalItemManager::~LebalItemManager()
{
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
    if (!m_currentImage.isNull()) {
        m_labelEditingWindow->labelEditing->loadImage(m_currentImage);
    }

    if (clearExisting) {
        emitDiagnoseAll();
    }
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
