#include "boardmanager.h"
#include "ui_boardmanager.h"

#include "../ComponentsDetect/yolomodel.h"
#include "../HDCamera/camerastation.h"
#include "../HDCamera/camerastationclient.h"
#include "../tool/lebalitemmanager.h"
#include "../tool/pcbextract.h"
#include "../tool/siftmatcher.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QSet>
#include <QByteArray>
#include <QAbstractItemView>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QPlainTextEdit>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <exception>
#include <functional>
#include <opencv2/opencv.hpp>

namespace {
cv::Mat qimageToBgrMat(const QImage &img)
{
    if (img.isNull()) {
        return {};
    }

    QImage rgb = img;
    if (rgb.format() != QImage::Format_RGB888) {
        rgb = rgb.convertToFormat(QImage::Format_RGB888);
    }

    cv::Mat rgbMat(rgb.height(), rgb.width(), CV_8UC3,
                   const_cast<uchar *>(rgb.bits()),
                   static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(rgbMat, bgr, cv::COLOR_RGB2BGR);
    return bgr.clone();
}

QJsonObject rectToJson(const QRectF &rect)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("x"), rect.x());
    obj.insert(QStringLiteral("y"), rect.y());
    obj.insert(QStringLiteral("w"), rect.width());
    obj.insert(QStringLiteral("h"), rect.height());
    return obj;
}

QRectF rectFromJson(const QJsonObject &obj)
{
    return QRectF(obj.value(QStringLiteral("x")).toDouble(),
                  obj.value(QStringLiteral("y")).toDouble(),
                  obj.value(QStringLiteral("w")).toDouble(),
                  obj.value(QStringLiteral("h")).toDouble());
}

QJsonObject pointToJson(const QPointF &point)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("x"), point.x());
    obj.insert(QStringLiteral("y"), point.y());
    return obj;
}

QPointF pointFromJson(const QJsonObject &obj)
{
    return QPointF(obj.value(QStringLiteral("x")).toDouble(),
                   obj.value(QStringLiteral("y")).toDouble());
}

QJsonObject temperatureSpecToJson(const TemperatureSpec &spec)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("monitorPoint"), pointToJson(spec.MonitorPoint));
    obj.insert(QStringLiteral("monitorPosition"), rectToJson(spec.MonitorPosition));
    obj.insert(QStringLiteral("alarmThresholdC"), spec.alarmThresholdC);
    obj.insert(QStringLiteral("needContinuousCapture"), spec.needContinuousCapture);
    obj.insert(QStringLiteral("captureMode"), spec.captureMode);
    return obj;
}

TemperatureSpec temperatureSpecFromJson(const QJsonObject &obj)
{
    TemperatureSpec spec;
    spec.MonitorPoint = pointFromJson(obj.value(QStringLiteral("monitorPoint")).toObject());
    spec.MonitorPosition = rectFromJson(obj.value(QStringLiteral("monitorPosition")).toObject());
    spec.alarmThresholdC = obj.value(QStringLiteral("alarmThresholdC")).toDouble(spec.alarmThresholdC);
    spec.needContinuousCapture = obj.value(QStringLiteral("needContinuousCapture")).toBool(spec.needContinuousCapture);
    spec.captureMode = obj.value(QStringLiteral("captureMode")).toString();
    return spec;
}
}

BoardManager::BoardManager(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::BoardManager)
{
    ui->setupUi(this);
    setupUiElements();
    setupConnections();
    loadBoardsFromStorage();
    populateBoardTable();
}

BoardManager::~BoardManager()
{
    delete ui;
}

void BoardManager::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void BoardManager::setupUiElements()
{
    if (ui->tableBoards) {
        ui->tableBoards->horizontalHeader()->setStretchLastSection(true);
        ui->tableBoards->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        ui->tableBoards->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->tableBoards->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->tableBoards->setEditTriggers(QAbstractItemView::NoEditTriggers);
    }

    if (ui->labelEditorContainer) {
        if (!ui->labelEditorContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->labelEditorContainer);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);
        }
        m_labelEditor = new LebalItemManager(ui->labelEditorContainer);
        ui->labelEditorContainer->layout()->addWidget(m_labelEditor);

        connect(m_labelEditor, &LebalItemManager::diagnoseRequested, this,
                [this](const QList<CompLabel> &labels) {
                    if (ui->labelComponentCount) {
                        ui->labelComponentCount->setText(QString::number(labels.size()));
                    }
                    if (ui->textOtherInfo) {
                        ui->textOtherInfo->append(QStringLiteral("标签更新: %1 个元件")
                                                      .arg(labels.size()));
                    }
                });
    }
}

void BoardManager::loadBoardsFromStorage()
{
    const QString baseDir = QCoreApplication::applicationDirPath();
    const QString storage = QDir(baseDir).filePath(QStringLiteral("board_db"));
    m_dataManager.setStorageDir(storage);
    m_dataManager.load();

    m_boards.clear();
    for (const auto &board : m_dataManager.boards()) {
        BoardEntry entry;
        entry.name = board.name;
        entry.model = board.boardId;
        entry.version = board.version;
        entry.created = board.createdAt;
        entry.sourcePath = board.imagePath;

        QVector<CompLabel> components;
        for (const auto &component : board.components) {
            const QString labelText = component.type.isEmpty() ? component.reference : component.type;
            components.append(CompLabel(0,
                                        component.bbox.x(),
                                        component.bbox.y(),
                                        component.bbox.width(),
                                        component.bbox.height(),
                                        0,
                                        0.0,
                                        labelText,
                                        component.reference,
                                        QByteArray()));
        }
        entry.components = components;

        m_boards.push_back(entry);
    }
}

void BoardManager::persistBoards()
{
    m_dataManager.save();
}

void BoardManager::setupConnections()
{
    if (ui->buttonSearch) {
        connect(ui->buttonSearch, &QPushButton::clicked, this, &BoardManager::onSearchBoards);
    }
    if (ui->buttonNewBoard) {
        connect(ui->buttonNewBoard, &QPushButton::clicked, this, &BoardManager::onNewBoard);
    }
    if (ui->buttonDeleteBoard) {
        connect(ui->buttonDeleteBoard, &QPushButton::clicked, this, &BoardManager::onDeleteBoard);
    }
    if (ui->buttonRefreshList) {
        connect(ui->buttonRefreshList, &QPushButton::clicked, this, &BoardManager::onRefreshBoards);
    }
    if (ui->buttonEditAnchors) {
        connect(ui->buttonEditAnchors, &QPushButton::clicked, this, &BoardManager::onEditAnchors);
    }
    if (ui->buttonEditPlanBindings) {
        connect(ui->buttonEditPlanBindings, &QPushButton::clicked, this, &BoardManager::onEditPlanBindings);
    }
    if (ui->tableBoards) {
        connect(ui->tableBoards, &QTableWidget::itemSelectionChanged, this, &BoardManager::onBoardSelectionChanged);
    }
}

QString BoardManager::currentBoardName() const
{
    if (!ui->tableBoards) {
        return QString();
    }
    const int row = ui->tableBoards->currentRow();
    if (row < 0 || row >= ui->tableBoards->rowCount()) {
        return QString();
    }
    const auto *item = ui->tableBoards->item(row, 0);
    return item ? item->text() : QString();
}

bool BoardManager::editJsonDialog(const QString &title, const QString &initialText, QString *updatedText)
{
    if (!updatedText) {
        return false;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(900, 650);

    auto *layout = new QVBoxLayout(&dialog);
    auto *editor = new QPlainTextEdit(&dialog);
    editor->setPlainText(initialText);
    layout->addWidget(editor);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    *updatedText = editor->toPlainText();
    return true;
}

void BoardManager::populateBoardTable(const QString &filterText)
{
    if (!ui->tableBoards) {
        return;
    }

    ui->tableBoards->setRowCount(0);
    const QString filter = filterText.trimmed();

    for (const auto &board : m_boards) {
        if (!filter.isEmpty()) {
            const bool matched = board.name.contains(filter, Qt::CaseInsensitive)
                || board.model.contains(filter, Qt::CaseInsensitive)
                || board.version.contains(filter, Qt::CaseInsensitive);
            if (!matched) {
                continue;
            }
        }

        const int row = ui->tableBoards->rowCount();
        ui->tableBoards->insertRow(row);
        ui->tableBoards->setItem(row, 0, new QTableWidgetItem(board.name));
        ui->tableBoards->setItem(row, 1, new QTableWidgetItem(board.model));
        ui->tableBoards->setItem(row, 2, new QTableWidgetItem(board.version));
        ui->tableBoards->setItem(row, 3, new QTableWidgetItem(board.created));
    }
}

int BoardManager::findBoardByName(const QString &name) const
{
    for (int i = 0; i < m_boards.size(); ++i) {
        if (m_boards[i].name.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

void BoardManager::updateInfoPanel(const BoardEntry &entry)
{
    if (ui->lineEditBoardName) {
        ui->lineEditBoardName->setText(entry.name);
    }
    if (ui->lineEditBoardModel) {
        ui->lineEditBoardModel->setText(entry.model);
    }
    if (ui->lineEditBoardVersion) {
        ui->lineEditBoardVersion->setText(entry.version);
    }
    if (ui->labelComponentCount) {
        ui->labelComponentCount->setText(QString::number(entry.components.size()));
    }
    if (ui->labelCreatedTime) {
        ui->labelCreatedTime->setText(entry.created.isEmpty() ? QStringLiteral("-") : entry.created);
    }
    if (ui->textOtherInfo) {
        ui->textOtherInfo->setText(QStringLiteral("板卡名称：%1\n类型：%2\n版本：%3")
                                       .arg(entry.name)
                                       .arg(entry.model)
                                       .arg(entry.version));
    }
}

void BoardManager::resetInfoPanel()
{
    if (ui->lineEditBoardName) {
        ui->lineEditBoardName->clear();
    }
    if (ui->lineEditBoardModel) {
        ui->lineEditBoardModel->clear();
    }
    if (ui->lineEditBoardVersion) {
        ui->lineEditBoardVersion->clear();
    }
    if (ui->labelComponentCount) {
        ui->labelComponentCount->setText(QStringLiteral("0"));
    }
    if (ui->labelCreatedTime) {
        ui->labelCreatedTime->setText(QStringLiteral("-"));
    }
    if (ui->textOtherInfo) {
        ui->textOtherInfo->clear();
    }
}

void BoardManager::onSearchBoards()
{
    populateBoardTable(ui->lineEditSearch ? ui->lineEditSearch->text() : QString());
}

void BoardManager::onNewBoard()
{
    QImage sourceImage;
    {
        QDialog sourceDialog(this);
        sourceDialog.setWindowTitle(tr("选择图像来源"));

        auto *layout = new QVBoxLayout(&sourceDialog);
        auto *hint = new QLabel(tr("请选择板卡图像来源"), &sourceDialog);
        layout->addWidget(hint);

        auto *buttonRow = new QHBoxLayout();
        auto *fileBtn = new QPushButton(tr("从文件选择"), &sourceDialog);
        auto *cameraBtn = new QPushButton(tr("相机采集"), &sourceDialog);
        buttonRow->addWidget(fileBtn);
        buttonRow->addWidget(cameraBtn);
        layout->addLayout(buttonRow);

        connect(fileBtn, &QPushButton::clicked, &sourceDialog, [&]() {
            const QString imagePath = QFileDialog::getOpenFileName(this, tr("选择板卡图片"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp)"));
            if (imagePath.isEmpty()) {
                return;
            }
            QImageReader reader(imagePath);
            reader.setAutoTransform(true);
            QImage image = reader.read();
            if (image.isNull()) {
                QMessageBox::warning(this, tr("新建板卡"), tr("无法读取图片。"));
                return;
            }
            sourceImage = image;
            sourceDialog.accept();
        });

        connect(cameraBtn, &QPushButton::clicked, &sourceDialog, [&]() {
            QDialog captureDialog(&sourceDialog);
            captureDialog.setWindowTitle(tr("相机采集"));
            captureDialog.resize(900, 600);

            auto *layout = new QVBoxLayout(&captureDialog);
            auto *preview = new QLabel(&captureDialog);
            preview->setMinimumHeight(420);
            preview->setAlignment(Qt::AlignCenter);
            preview->setText(tr("等待图像"));
            layout->addWidget(preview);

            auto *buttons = new QDialogButtonBox(&captureDialog);
            auto *captureBtn = buttons->addButton(tr("拍照"), QDialogButtonBox::AcceptRole);
            auto *cancelBtn = buttons->addButton(QDialogButtonBox::Cancel);
            layout->addWidget(buttons);

            QImage latest;
            CameraStation::instance()->start();
            CameraStationClient client(&captureDialog);
            client.setEnabled(true);
            client.requestConfigure(CameraStation::Config());

            connect(&client, &CameraStationClient::imageCaptured, &captureDialog, [&latest, preview](const ImageData &img) {
                if (img.image.isNull()) {
                    return;
                }
                latest = img.image;
                preview->setPixmap(QPixmap::fromImage(latest).scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            });

            connect(captureBtn, &QPushButton::clicked, &captureDialog, [&]() {
                if (latest.isNull()) {
                    QMessageBox::information(&captureDialog, tr("相机采集"), tr("尚未获取到图像。"));
                    return;
                }
                sourceImage = latest;
                captureDialog.accept();
            });
            connect(cancelBtn, &QAbstractButton::clicked, &captureDialog, &QDialog::reject);

            if (captureDialog.exec() == QDialog::Accepted) {
                sourceDialog.accept();
            }
        });

        if (sourceDialog.exec() != QDialog::Accepted) {
            return;
        }
    }

    if (sourceImage.isNull()) {
        return;
    }

    const QString baseDir = QCoreApplication::applicationDirPath();
    const QDir storageDir(QDir(baseDir).filePath(QStringLiteral("board_db")));
    const QString imageDir = storageDir.filePath(QStringLiteral("images"));
    QDir().mkpath(imageDir);
    const QString savedName = QStringLiteral("board_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"));
    const QString savedPath = QDir(imageDir).filePath(savedName);
    if (!sourceImage.save(savedPath)) {
        QMessageBox::warning(this, tr("新建板卡"), tr("保存图片失败。"));
        return;
    }

    QImage pcbImage = sourceImage;
    QRectF pcbRect;
    bool useManualCrop = true;
    {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("板卡分割"));
        dialog.resize(1000, 700);

        auto *layout = new QVBoxLayout(&dialog);
        auto *extract = new PCBExtractWidget(&dialog);
        extract->setInputImage(sourceImage);
        layout->addWidget(extract);

        auto *useCropCheck = new QCheckBox(tr("使用手动裁剪用于识别"), &dialog);
        useCropCheck->setChecked(true);
        layout->addWidget(useCropCheck);

        connect(extract, &PCBExtractWidget::confirmed, &dialog,
                [&pcbImage, &pcbRect, &useManualCrop, useCropCheck, &dialog](const QImage &, const QImage &cropped, const QRectF &rect) {
                    if (!cropped.isNull()) {
                        pcbImage = cropped;
                        pcbRect = rect;
                    }
                    useManualCrop = (useCropCheck && useCropCheck->isChecked());
                    dialog.accept();
                });

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
    }

    const cv::Mat sourceBgr = qimageToBgrMat(sourceImage);
    if (sourceBgr.empty()) {
        QMessageBox::warning(this, tr("新建板卡"), tr("图片格式不支持。"));
        return;
    }

    QVector<CompLabel> labels;
    try {
        std::vector<CompLabel> results;
        if (useManualCrop && pcbRect.isValid() && !pcbRect.isNull()) {
            const cv::Mat cropBgr = qimageToBgrMat(pcbImage);
            if (cropBgr.empty()) {
                QMessageBox::warning(this, tr("新建板卡"), tr("裁剪图片格式不支持。"));
                return;
            }
            results = YOLOModel::getInstance()->infer(cropBgr, false);
        } else {
            results = YOLOModel::getInstance()->infer(sourceBgr, true);
        }
        labels.reserve(static_cast<int>(results.size()));
        for (const auto &label : results) {
            labels.append(label);
        }
    } catch (const std::exception &ex) {
        QMessageBox::warning(this, tr("新建板卡"), tr("元件识别失败: %1").arg(ex.what()));
        return;
    }

    if (useManualCrop && pcbRect.isValid() && !pcbRect.isNull()) {
        const double offsetX = pcbRect.x();
        const double offsetY = pcbRect.y();
        const double maxW = sourceImage.width();
        const double maxH = sourceImage.height();
        for (auto &label : labels) {
            label.x = std::clamp(label.x + offsetX, 0.0, std::max(0.0, maxW - 1.0));
            label.y = std::clamp(label.y + offsetY, 0.0, std::max(0.0, maxH - 1.0));
            label.w = std::clamp(label.w, 0.0, std::max(0.0, maxW - label.x));
            label.h = std::clamp(label.h, 0.0, std::max(0.0, maxH - label.y));
        }
    }

    QDialog infoDialog(this);
    infoDialog.setWindowTitle(tr("板卡信息"));
    auto *layout = new QVBoxLayout(&infoDialog);
    auto *form = new QFormLayout();
    auto *nameEdit = new QLineEdit(&infoDialog);
    auto *modelEdit = new QLineEdit(&infoDialog);
    form->addRow(tr("名称"), nameEdit);
    form->addRow(tr("型号"), modelEdit);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &infoDialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &infoDialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &infoDialog, &QDialog::reject);

    if (infoDialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::information(this, tr("新建板卡"), tr("请输入板卡名称。"));
        return;
    }

    BoardInfoRecord record;
    record.name = name;
    record.boardId = modelEdit->text().trimmed();
    record.version = QStringLiteral("v1");
    record.createdAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    record.imagePath = savedPath;

    if (!m_dataManager.addBoard(record)) {
        QMessageBox::warning(this, tr("新建板卡"), tr("板卡已存在或数据无效。"));
        return;
    }

    for (const auto &label : labels) {
        QString id = m_dataManager.normalizeComponentReference(name, label.position_number);
        if (id.isEmpty()) {
            id = m_dataManager.normalizeComponentReference(name, QString());
        }
        if (id.isEmpty()) {
            continue;
        }

        BoardComponentRecord component;
        component.reference = id;
        component.type = label.label;
        component.model = QString();
        component.bbox = QRectF(label.x, label.y, label.w, label.h);

        m_dataManager.upsertComponent(name, component);
    }

    const std::vector<std::string> ids = {name.toStdString()};
    const std::vector<cv::Mat> images = {sourceBgr};
    SIFT_MATCHER->appendToDatabase(ids, images);

    persistBoards();
    loadBoardsFromStorage();
    populateBoardTable(ui->lineEditSearch ? ui->lineEditSearch->text() : QString());
}

void BoardManager::onDeleteBoard()
{
    if (!ui->tableBoards) {
        return;
    }

    const int row = ui->tableBoards->currentRow();
    if (row < 0 || row >= ui->tableBoards->rowCount()) {
        QMessageBox::information(this, tr("Delete Board"), tr("请选择要删除的板卡。"));
        return;
    }

    const QString name = ui->tableBoards->item(row, 0) ? ui->tableBoards->item(row, 0)->text() : QString();
    if (!name.isEmpty()) {
        m_dataManager.removeBoard(name);
        persistBoards();
        loadBoardsFromStorage();
    }
    populateBoardTable(ui->lineEditSearch ? ui->lineEditSearch->text() : QString());
}

void BoardManager::onRefreshBoards()
{
    if (ui->lineEditSearch) {
        ui->lineEditSearch->clear();
    }
    loadBoardsFromStorage();
    populateBoardTable();
}

void BoardManager::onEditAnchors()
{
    const QString boardName = currentBoardName();
    if (boardName.isEmpty()) {
        QMessageBox::information(this, tr("编辑锚点"), tr("请选择板卡。"));
        return;
    }

    QJsonArray records;
    for (const auto &record : m_dataManager.anchors()) {
        if (record.boardName.compare(boardName, Qt::CaseInsensitive) != 0) {
            continue;
        }

        QJsonArray anchors;
        for (const auto &anchor : record.anchors) {
            QJsonObject anchorObj;
            anchorObj.insert(QStringLiteral("id"), anchor.id);
            anchorObj.insert(QStringLiteral("label"), anchor.label);
            anchorObj.insert(QStringLiteral("position"), rectToJson(anchor.position));
            anchors.append(anchorObj);
        }

        QJsonObject recordObj;
        recordObj.insert(QStringLiteral("componentRef"), record.componentRef);
        recordObj.insert(QStringLiteral("anchors"), anchors);
        records.append(recordObj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("boardName"), boardName);
    root.insert(QStringLiteral("anchors"), records);

    const QString initialText = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    QString updatedText;
    if (!editJsonDialog(tr("编辑锚点"), initialText, &updatedText)) {
        return;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(updatedText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, tr("编辑锚点"), tr("JSON 解析失败：%1").arg(error.errorString()));
        return;
    }

    const QJsonObject obj = doc.object();
    const QJsonArray list = obj.value(QStringLiteral("anchors")).toArray();

    m_dataManager.clearAnchors(boardName);
    for (const auto &value : list) {
        const QJsonObject recordObj = value.toObject();
        QString componentRef = recordObj.value(QStringLiteral("componentRef")).toString();
        if (componentRef.trimmed().isEmpty()) {
            componentRef = m_dataManager.normalizeComponentReference(boardName, QString());
        }
        if (componentRef.trimmed().isEmpty()) {
            continue;
        }

        QVector<AnchorPoint> anchors;
        const QJsonArray anchorsArray = recordObj.value(QStringLiteral("anchors")).toArray();
        for (const auto &anchorValue : anchorsArray) {
            const QJsonObject anchorObj = anchorValue.toObject();
            AnchorPoint anchor;
            anchor.id = anchorObj.value(QStringLiteral("id")).toString();
            anchor.label = anchorObj.value(QStringLiteral("label")).toString();
            anchor.position = rectFromJson(anchorObj.value(QStringLiteral("position")).toObject());
            anchors.push_back(anchor);
        }

        m_dataManager.upsertAnchors(boardName, componentRef, anchors);
    }

    persistBoards();
    loadBoardsFromStorage();
}

void BoardManager::onEditPlanBindings()
{
    const QString boardName = currentBoardName();
    if (boardName.isEmpty()) {
        QMessageBox::information(this, tr("编辑计划绑定"), tr("请选择板卡。"));
        return;
    }

    QJsonArray bindings;
    for (const auto &record : m_dataManager.planBindings()) {
        if (record.boardName.compare(boardName, Qt::CaseInsensitive) != 0) {
            continue;
        }

        for (const auto &binding : record.bindings) {
            QJsonObject params;
            for (auto it = binding.parameterValues.cbegin(); it != binding.parameterValues.cend(); ++it) {
                params.insert(it.key(), QJsonValue::fromVariant(it.value()));
            }

            QJsonObject bindingObj;
            bindingObj.insert(QStringLiteral("componentRef"), binding.componentRef);
            bindingObj.insert(QStringLiteral("planId"), binding.planId);
            bindingObj.insert(QStringLiteral("parameterValues"), params);
            bindingObj.insert(QStringLiteral("hasTemperatureOverride"), binding.hasTemperatureOverride);
            bindingObj.insert(QStringLiteral("temperatureOverride"), temperatureSpecToJson(binding.temperatureOverride));
            bindings.append(bindingObj);
        }
        break;
    }

    QJsonObject root;
    root.insert(QStringLiteral("boardName"), boardName);
    root.insert(QStringLiteral("bindings"), bindings);

    const QString initialText = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    QString updatedText;
    if (!editJsonDialog(tr("编辑计划绑定"), initialText, &updatedText)) {
        return;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(updatedText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, tr("编辑计划绑定"), tr("JSON 解析失败：%1").arg(error.errorString()));
        return;
    }

    const QJsonObject obj = doc.object();
    const QJsonArray list = obj.value(QStringLiteral("bindings")).toArray();

    m_dataManager.clearPlanBindings(boardName);
    for (const auto &value : list) {
        const QJsonObject bindingObj = value.toObject();
        ComponentPlanBinding binding;
        binding.componentRef = bindingObj.value(QStringLiteral("componentRef")).toString();
        if (binding.componentRef.trimmed().isEmpty()) {
            binding.componentRef = m_dataManager.normalizeComponentReference(boardName, QString());
        }
        binding.planId = bindingObj.value(QStringLiteral("planId")).toString();

        const QJsonObject paramsObj = bindingObj.value(QStringLiteral("parameterValues")).toObject();
        for (auto it = paramsObj.begin(); it != paramsObj.end(); ++it) {
            binding.parameterValues.insert(it.key(), it.value().toVariant());
        }

        binding.hasTemperatureOverride = bindingObj.value(QStringLiteral("hasTemperatureOverride")).toBool(false);
        binding.temperatureOverride = temperatureSpecFromJson(bindingObj.value(QStringLiteral("temperatureOverride")).toObject());

        if (!binding.componentRef.trimmed().isEmpty() && !binding.planId.trimmed().isEmpty()) {
            m_dataManager.upsertPlanBinding(boardName, binding);
        }
    }

    persistBoards();
    loadBoardsFromStorage();
}


void BoardManager::onOpenLabelEditor()
{
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("标签编辑"));
    dialog->resize(1100, 800);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *editor = new LebalItemManager(dialog);
    layout->addWidget(editor);

    connect(editor, &LebalItemManager::diagnoseRequested, this,
            [this](const QList<CompLabel> &labels) {
                if (!ui->tableBoards) {
                    return;
                }
                const int row = ui->tableBoards->currentRow();
                if (row < 0 || row >= ui->tableBoards->rowCount()) {
                    return;
                }
                const QString boardName = ui->tableBoards->item(row, 0)
                                              ? ui->tableBoards->item(row, 0)->text()
                                              : QString();
                if (boardName.isEmpty()) {
                    return;
                }

                QSet<QString> newIds;
                for (const auto &label : labels) {
                    QString id = m_dataManager.normalizeComponentReference(boardName, label.position_number);
                    if (id.isEmpty()) {
                        id = m_dataManager.normalizeComponentReference(boardName, QString());
                    }
                    if (id.isEmpty()) {
                        continue;
                    }
                    newIds.insert(id);

                    BoardComponentRecord record;
                    record.reference = id;
                    record.type = label.label;
                    record.model = QString();
                    record.bbox = QRectF(label.x, label.y, label.w, label.h);

                    m_dataManager.upsertComponent(boardName, record);
                }

                const int boardIndex = findBoardByName(boardName);
                if (boardIndex >= 0) {
                    const auto existingIds = m_boards[boardIndex].components;
                    for (const auto &comp : existingIds) {
                        const QString id = comp.position_number;
                        if (!newIds.contains(id)) {
                            m_dataManager.removeComponent(boardName, id);
                        }
                    }
                }

                persistBoards();
                loadBoardsFromStorage();
                const int refreshed = findBoardByName(boardName);
                if (refreshed >= 0) {
                    updateInfoPanel(m_boards[refreshed]);
                }
            });

    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->exec();
}

void BoardManager::onBoardSelectionChanged()
{
    if (!ui->tableBoards || ui->tableBoards->currentRow() < 0) {
        return;
    }

    const int row = ui->tableBoards->currentRow();
    const QString name = ui->tableBoards->item(row, 0) ? ui->tableBoards->item(row, 0)->text() : QString();
    const int index = findBoardByName(name);
    if (index < 0) {
        return;
    }

    const auto &entry = m_boards[index];
    if (ui->lineEditBoardName) {
        ui->lineEditBoardName->setText(entry.name);
    }
    if (ui->lineEditBoardModel) {
        ui->lineEditBoardModel->setText(entry.model);
    }
    if (ui->lineEditBoardVersion) {
        ui->lineEditBoardVersion->setText(entry.version);
    }
    updateInfoPanel(entry);

    if (m_labelEditor) {
        if (!entry.sourcePath.isEmpty()) {
            QImage image(entry.sourcePath);
            if (!image.isNull()) {
                m_labelEditor->setImage(image);
            }
        }
        QList<CompLabel> labels;
        int nextId = 1;
        for (const auto &component : entry.components) {
            const QString labelText = component.label.isEmpty() ? component.position_number : component.label;
            labels.append(CompLabel(nextId++,
                                    component.x,
                                    component.y,
                                    component.w,
                                    component.h,
                                    0,
                                    0.0,
                                    labelText,
                                    component.position_number,
                                    QByteArray()));
        }
        m_labelEditor->setImage(entry.sourceImage);
        m_labelEditor->setLabels(labels, true);
    }
}
