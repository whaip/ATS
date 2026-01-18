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
#include <QJsonObject>
#include <QSet>
#include <QByteArray>
#include <QAbstractItemView>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QResizeEvent>

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
        entry.model = board.model;
        entry.version = board.version;
        entry.created = board.createdAt;
        entry.sourcePath = board.imagePath;

        QVector<CompLabel> components;
        for (const auto &plan : m_dataManager.plans()) {
            if (plan.boardName.compare(board.name, Qt::CaseInsensitive) == 0) {
                for (const auto &component : plan.components) {
                    components.append(CompLabel(0,
                                                component.wiringRect.x(),
                                                component.wiringRect.y(),
                                                component.wiringRect.width(),
                                                component.wiringRect.height(),
                                                0,
                                                0.0,
                                                component.id,
                                                component.id,
                                                QByteArray()));
                }
                break;
            }
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
    if (ui->tableBoards) {
        connect(ui->tableBoards, &QTableWidget::itemSelectionChanged, this, &BoardManager::onBoardSelectionChanged);
    }
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
    record.model = modelEdit->text().trimmed();
    record.version = QStringLiteral("v1");
    record.createdAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    record.imagePath = savedPath;

    if (!m_dataManager.addBoard(record)) {
        QMessageBox::warning(this, tr("新建板卡"), tr("板卡已存在或数据无效。"));
        return;
    }

    for (const auto &label : labels) {
        QString id = label.position_number;
        if (id.isEmpty()) {
            id = label.label;
        }
        if (id.isEmpty()) {
            id = QString::number(label.id);
        }
        if (id.isEmpty()) {
            continue;
        }

        BoardComponentRecord component;
        component.id = id;
        component.wiringRect = QRectF(label.x, label.y, label.w, label.h);

        QJsonObject params;
        params.insert(QStringLiteral("label"), label.label);
        params.insert(QStringLiteral("confidence"), label.confidence);
        params.insert(QStringLiteral("notes"), QString::fromUtf8(label.notes.toBase64()));
        component.params = params;

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
                    QString id = label.position_number;
                    if (id.isEmpty()) {
                        id = label.label;
                    }
                    if (id.isEmpty()) {
                        id = QString::number(label.id);
                    }
                    if (id.isEmpty()) {
                        continue;
                    }
                    newIds.insert(id);

                    BoardComponentRecord record;
                    record.id = id;
                    record.wiringRect = QRectF(label.x, label.y, label.w, label.h);

                    QJsonObject params;
                    params.insert(QStringLiteral("label"), label.label);
                    params.insert(QStringLiteral("confidence"), label.confidence);
                    params.insert(QStringLiteral("notes"), QString::fromUtf8(label.notes.toBase64()));
                    record.params = params;

                    m_dataManager.upsertComponent(boardName, record);
                }

                const int boardIndex = findBoardByName(boardName);
                if (boardIndex >= 0) {
                    const auto existingIds = m_boards[boardIndex].components;
                    for (const auto &comp : existingIds) {
                        const QString id = comp.position_number.isEmpty() ? comp.label : comp.position_number;
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
        for (const auto &plan : m_dataManager.plans()) {
            if (plan.boardName.compare(entry.name, Qt::CaseInsensitive) != 0) {
                continue;
            }
            for (const auto &component : plan.components) {
                const QJsonObject params = component.params;
                const QString labelText = params.value(QStringLiteral("label")).toString(component.id);
                const double confidence = params.value(QStringLiteral("confidence")).toDouble(0.0);
                const QString notesStr = params.value(QStringLiteral("notes")).toString();
                const QByteArray notes = notesStr.isEmpty() ? QByteArray() : QByteArray::fromBase64(notesStr.toUtf8());

                labels.append(CompLabel(nextId++,
                                        component.wiringRect.x(),
                                        component.wiringRect.y(),
                                        component.wiringRect.width(),
                                        component.wiringRect.height(),
                                        0,
                                        confidence,
                                        labelText,
                                        component.id,
                                        notes));
            }
            break;
        }
        m_labelEditor->setImage(entry.sourceImage);
        m_labelEditor->setLabels(labels, true);
    }
}
