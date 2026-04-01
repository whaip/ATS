#include "boardmanager.h"
#include "ui_boardmanager.h"

#include "../boardrepository.h"
#include "../componenttyperegistry.h"
#include "../ComponentsDetect/yolomodel.h"
#include "../HDCamera/camerastation.h"
#include "../HDCamera/camerastationclient.h"
#include "../tpsparamservice.h"
#include "../tool/lebalitemmanager.h"
#include "../tool/pcbextract.h"
#include "../tool/siftmatcher.h"
#include "../logger.h"
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>
#include <exception>
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

QString componentTypeNameByClassId(int cls)
{
    const ComponentTypeRegistryData registry = ComponentTypeRegistry::load();
    return ComponentTypeRegistry::typeNameFromClassId(cls, registry);
}

QImage loadImageByDbPath(const QString &imagePath, const QString &databasePath)
{
    if (imagePath.trimmed().isEmpty()) {
        return {};
    }

    QFileInfo imageInfo(imagePath);
    if (imageInfo.isRelative() && !databasePath.isEmpty()) {
        imageInfo = QFileInfo(QFileInfo(databasePath).dir(), imagePath);
    }

    return QImage(imageInfo.absoluteFilePath());
}

QString resolveDescriptorDbPath(const QString &boardDbPath)
{
    if (boardDbPath.trimmed().isEmpty()) {
        return QStringLiteral("descriptors_database.yml");
    }
    return QFileInfo(boardDbPath).dir().filePath(QStringLiteral("descriptors_database.yml"));
}
}

BoardManager::BoardManager(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::BoardManager)
    , m_repository(new BoardsRepository(this))
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

QString BoardManager::resolveDatabasePath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;

    candidates << QDir(appDir).filePath(QStringLiteral("board_db/boards.json"));

    QDir probeDir(appDir);
    for (int depth = 0; depth < 8; ++depth) {
        candidates << probeDir.filePath(QStringLiteral("board_db/boards.json"));
        if (!probeDir.cdUp()) {
            break;
        }
    }

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return QDir::cleanPath(candidates.first());
}

QString BoardManager::resolveImagePath(const QString &imagePath) const
{
    if (imagePath.trimmed().isEmpty()) {
        return {};
    }

    QFileInfo imageInfo(imagePath);
    if (!imageInfo.isAbsolute()) {
        imageInfo = QFileInfo(QFileInfo(m_databasePath).dir(), imagePath);
    }
    return imageInfo.absoluteFilePath();
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

    if (ui->buttonEditPlanBindings) {
        ui->buttonEditPlanBindings->show();
        ui->buttonEditPlanBindings->setText(tr("开始测试"));
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
                });
        connect(m_labelEditor, &LebalItemManager::selectedLabelsChanged, this,
                [this](const QList<CompLabel> &labels) {
                    if (!ui || !ui->textOtherInfo) {
                        return;
                    }
                    const QString current = ui->textOtherInfo->toPlainText();
                    const QString selectedLine = QStringLiteral("已选元件：%1").arg(labels.size());
                    if (!current.contains(QStringLiteral("已选元件："))) {
                        ui->textOtherInfo->setText(current + QStringLiteral("\n") + selectedLine);
                    } else {
                        QString updated = current;
                        updated.replace(QRegularExpression(QStringLiteral("已选元件：\\d+")), selectedLine);
                        ui->textOtherInfo->setText(updated);
                    }
                });
        connect(m_labelEditor, &LebalItemManager::labelsChanged, this, &BoardManager::onLabelsChanged);
    }
}

BoardManager::BoardEntry BoardManager::buildEntryFromRepositoryBoard(int index) const
{
    BoardEntry entry;
    const BoardRecord *board = m_repository->boardAt(index);
    if (!board) {
        return entry;
    }

    entry.boardId = board->boardId;
    entry.name = board->name;
    entry.model = board->boardId;
    entry.version = board->version;
    entry.created = board->createdAt;
    entry.sourcePath = board->imagePath;

    entry.components.reserve(board->components.size());
    for (const auto &component : board->components) {
        entry.components.push_back(component.label);
    }
    return entry;
}

void BoardManager::loadBoardsFromStorage()
{
    m_databasePath = resolveDatabasePath();
    SIFT_MATCHER->setDatabasePath(resolveDescriptorDbPath(m_databasePath).toStdString());

    QFileInfo dbInfo(m_databasePath);
    if (!dbInfo.exists()) {
        QDir dir = dbInfo.dir();
        dir.mkpath(QStringLiteral("."));
        QFile initFile(m_databasePath);
        if (initFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            initFile.write("{\"boards\":[]}");
            initFile.close();
        }
    }

    QString errorMessage;
    if (!m_repository->load(m_databasePath, &errorMessage)) {
        m_boards.clear();
        m_rowToRepositoryIndex.clear();
        resetInfoPanel();
        if (ui->textOtherInfo) {
            ui->textOtherInfo->setText(errorMessage);
        }
        return;
    }

    if (m_labelEditor) {
        const QString paramPath = QFileInfo(m_databasePath).dir().filePath(QStringLiteral("i_params_db.json"));
        m_labelEditor->setTpsParameterDatabasePath(paramPath);
    }

    m_boards.clear();
    m_boards.reserve(m_repository->boardCount());
    for (int i = 0; i < m_repository->boardCount(); ++i) {
        m_boards.push_back(buildEntryFromRepositoryBoard(i));
    }
}

void BoardManager::persistBoards()
{
    QString errorMessage;
    if (!m_repository->save(&errorMessage) && ui->textOtherInfo) {
        ui->textOtherInfo->setText(QStringLiteral("保存失败：%1").arg(errorMessage));
    }
}

void BoardManager::setupConnections()
{
    if (ui->buttonSearch) {
        connect(ui->buttonSearch, &QPushButton::clicked, this, &BoardManager::onSearchBoards);
    }
    if (ui->buttonNewBoard) {
        connect(ui->buttonNewBoard, &QPushButton::clicked, this, &BoardManager::onNewBoard);
    }
    if (ui->buttonAutoRecognizeBoard) {
        connect(ui->buttonAutoRecognizeBoard, &QPushButton::clicked, this, &BoardManager::onAutoRecognizeBoard);
    }
    if (ui->buttonDeleteBoard) {
        connect(ui->buttonDeleteBoard, &QPushButton::clicked, this, &BoardManager::onDeleteBoard);
    }
    if (ui->buttonRefreshList) {
        connect(ui->buttonRefreshList, &QPushButton::clicked, this, &BoardManager::onRefreshBoards);
    }
    if (ui->buttonEditPlanBindings) {
        connect(ui->buttonEditPlanBindings, &QPushButton::clicked, this, &BoardManager::onStartSelectedTest);
    }
    if (ui->tableBoards) {
        connect(ui->tableBoards, &QTableWidget::itemSelectionChanged, this, &BoardManager::onBoardSelectionChanged);
    }
}

bool BoardManager::captureImageFromCamera(QImage *capturedImage)
{
    if (!capturedImage) {
        return false;
    }

    QDialog captureDialog(this);
    captureDialog.setWindowTitle(tr("高清相机采集"));
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
        *capturedImage = latest;
        captureDialog.accept();
    });
    connect(cancelBtn, &QAbstractButton::clicked, &captureDialog, &QDialog::reject);

    return captureDialog.exec() == QDialog::Accepted;
}

bool BoardManager::extractRoiForRecognition(const QImage &sourceImage,
                                            QImage *roiImage,
                                            bool *roiIsManualCrop)
{
    if (!roiImage || sourceImage.isNull()) {
        return false;
    }

    QImage selectedImage = sourceImage;
    bool useManualCrop = true;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("识别ROI提取"));
    dialog.resize(1000, 700);

    auto *layout = new QVBoxLayout(&dialog);
    auto *extract = new PCBExtractWidget(&dialog);
    extract->setInputImage(sourceImage);
    layout->addWidget(extract);

    auto *useCropCheck = new QCheckBox(tr("使用ROI区域用于板卡匹配"), &dialog);
    useCropCheck->setChecked(true);
    layout->addWidget(useCropCheck);

    connect(extract, &PCBExtractWidget::confirmed, &dialog,
            [&selectedImage, &useManualCrop, useCropCheck, &dialog](const QImage &, const QImage &cropped, const QRectF &) {
                if (!cropped.isNull()) {
                    selectedImage = cropped;
                }
                useManualCrop = (useCropCheck && useCropCheck->isChecked());
                dialog.accept();
            });

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    *roiImage = useManualCrop ? selectedImage : sourceImage;
    if (roiIsManualCrop) {
        *roiIsManualCrop = useManualCrop;
    }
    return !roiImage->isNull();
}

void BoardManager::selectBoardByRepositoryIndex(int repositoryIndex)
{
    if (!ui->tableBoards || repositoryIndex < 0 || repositoryIndex >= m_boards.size()) {
        return;
    }

    auto findRow = [this, repositoryIndex]() -> int {
        for (int row = 0; row < m_rowToRepositoryIndex.size(); ++row) {
            if (m_rowToRepositoryIndex.at(row) == repositoryIndex) {
                return row;
            }
        }
        return -1;
    };

    int row = findRow();
    if (row < 0) {
        if (ui->lineEditSearch) {
            ui->lineEditSearch->clear();
        }
        populateBoardTable();
        row = findRow();
    }

    if (row < 0) {
        return;
    }

    ui->tableBoards->setCurrentCell(row, 0);
    ui->tableBoards->selectRow(row);
    if (auto *item = ui->tableBoards->item(row, 0)) {
        ui->tableBoards->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    }
}

void BoardManager::onAutoRecognizeBoard()
{
    Logger::log(QStringLiteral("BoardManager auto recognize start: boardCount=%1")
                    .arg(m_repository ? m_repository->boardCount() : 0),
                Logger::Level::Info);
    if (!m_repository || m_repository->boardCount() <= 0) {
        Logger::log(QStringLiteral("BoardManager auto recognize aborted: repository empty"),
                    Logger::Level::Warn);
        QMessageBox::information(this, tr("自动识别"), tr("当前数据库没有板卡，请先录入板卡。"));
        return;
    }

    QImage cameraImage;
    if (!captureImageFromCamera(&cameraImage) || cameraImage.isNull()) {
        Logger::log(QStringLiteral("BoardManager auto recognize aborted: camera capture failed"),
                    Logger::Level::Warn);
        return;
    }
    Logger::log(QStringLiteral("BoardManager image captured: size=%1x%2")
                    .arg(cameraImage.width())
                    .arg(cameraImage.height()),
                Logger::Level::Info);

    QImage roiImage;
    bool roiIsManualCrop = false;
    if (!extractRoiForRecognition(cameraImage, &roiImage, &roiIsManualCrop) || roiImage.isNull()) {
        Logger::log(QStringLiteral("BoardManager auto recognize aborted: ROI extraction canceled/failed"),
                    Logger::Level::Warn);
        return;
    }
    Logger::log(QStringLiteral("BoardManager ROI ready: size=%1x%2")
                    .arg(roiImage.width())
                    .arg(roiImage.height()),
                Logger::Level::Info);

    const cv::Mat matchInputBgr = qimageToBgrMat(roiImage);
    if (matchInputBgr.empty()) {
        Logger::log(QStringLiteral("BoardManager auto recognize failed: unsupported ROI format"),
                    Logger::Level::Error);
        QMessageBox::warning(this, tr("自动识别"), tr("ROI图像格式不支持，无法进行匹配。"));
        return;
    }

    const auto matchResults = SIFT_MATCHER->matchImage(matchInputBgr, roiIsManualCrop);
    Logger::log(QStringLiteral("BoardManager SIFT match complete: candidateCount=%1")
                    .arg(matchResults.size()),
                Logger::Level::Info);
    if (matchResults.empty()) {
        QMessageBox::information(this, tr("自动识别"), tr("SIFT未找到可用匹配结果。"));
        return;
    }

    QVector<QPair<int, SiftMatcher::MatchResult>> dbMatchedResults;
    for (const auto &result : matchResults) {
        const QString boardId = QString::fromStdString(result.boardId).trimmed();
        if (boardId.isEmpty()) {
            continue;
        }
        const int index = m_repository->indexOfBoard(boardId);
        if (index >= 0) {
            dbMatchedResults.push_back(qMakePair(index, result));
        }
    }

    if (dbMatchedResults.isEmpty()) {
        Logger::log(QStringLiteral("BoardManager SIFT candidates missing in repository"),
                    Logger::Level::Warn);
        QStringList candidates;
        const int count = std::min<int>(3, static_cast<int>(matchResults.size()));
        for (int i = 0; i < count; ++i) {
            candidates << QString::fromStdString(matchResults[i].boardId);
        }
        QMessageBox::information(this,
                                 tr("自动识别"),
                                 tr("识别到了候选型号，但在数据库中未找到对应板卡。候选：%1")
                                     .arg(candidates.join(QStringLiteral("、"))));
        return;
    }

    int matchedRepoIndex = dbMatchedResults.first().first;
    SiftMatcher::MatchResult matchedResult = dbMatchedResults.first().second;

    if (dbMatchedResults.size() > 1) {
        Logger::log(QStringLiteral("BoardManager multiple candidates: count=%1")
                        .arg(dbMatchedResults.size()),
                    Logger::Level::Info);
        const int topCount = std::min<int>(5, dbMatchedResults.size());
        QDialog candidateDialog(this);
        candidateDialog.setWindowTitle(tr("自动识别候选"));
        candidateDialog.resize(760, 520);

        auto *layout = new QVBoxLayout(&candidateDialog);
        auto *tipLabel = new QLabel(tr("识别到多个候选板卡，请选择匹配结果："), &candidateDialog);
        layout->addWidget(tipLabel);

        auto *candidateList = new QListWidget(&candidateDialog);
        candidateList->setIconSize(QSize(180, 120));
        candidateList->setSelectionMode(QAbstractItemView::SingleSelection);
        layout->addWidget(candidateList, 1);

        for (int i = 0; i < topCount; ++i) {
            const int repositoryIndex = dbMatchedResults.at(i).first;
            const auto &candidate = dbMatchedResults.at(i).second;
            const BoardRecord *board = m_repository->boardAt(repositoryIndex);
            const QString boardName = board ? board->name : QString::fromStdString(candidate.boardId);
            const QString boardId = QString::fromStdString(candidate.boardId);
            const QString text = QStringLiteral("%1（%2）\n分数:%3  特征:%4")
                                     .arg(boardName)
                                     .arg(boardId)
                                     .arg(QString::number(candidate.matchScore, 'f', 2))
                                     .arg(candidate.goodMatchCount);

            auto *item = new QListWidgetItem(text, candidateList);
            item->setData(Qt::UserRole, i);

            if (board) {
                const QImage boardImage = loadImageByDbPath(board->imagePath, m_databasePath);
                if (!boardImage.isNull()) {
                    const QPixmap preview = QPixmap::fromImage(
                        boardImage.scaled(candidateList->iconSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    item->setIcon(QIcon(preview));
                }
            }

            if (i == 0) {
                candidateList->setCurrentItem(item);
            }
        }

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &candidateDialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &candidateDialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &candidateDialog, &QDialog::reject);
        connect(candidateList, &QListWidget::itemDoubleClicked, &candidateDialog, [&candidateDialog](QListWidgetItem *) {
            candidateDialog.accept();
        });

        if (candidateDialog.exec() != QDialog::Accepted) {
            Logger::log(QStringLiteral("BoardManager candidate selection canceled"),
                        Logger::Level::Info);
            return;
        }

        const QListWidgetItem *selectedItem = candidateList->currentItem();
        if (!selectedItem) {
            return;
        }

        const int selectedIndex = selectedItem->data(Qt::UserRole).toInt();
        if (selectedIndex >= 0 && selectedIndex < topCount) {
            matchedRepoIndex = dbMatchedResults.at(selectedIndex).first;
            matchedResult = dbMatchedResults.at(selectedIndex).second;
        }
    }

    selectBoardByRepositoryIndex(matchedRepoIndex);
    Logger::log(QStringLiteral("BoardManager auto recognize selected: repoIndex=%1 boardId=%2 score=%3 goodMatches=%4")
                    .arg(matchedRepoIndex)
                    .arg(QString::fromStdString(matchedResult.boardId))
                    .arg(QString::number(matchedResult.matchScore, 'f', 2))
                    .arg(matchedResult.goodMatchCount),
                Logger::Level::Info);

    if (ui->textOtherInfo && matchedRepoIndex >= 0 && matchedRepoIndex < m_boards.size()) {
        const QString summary = QStringLiteral("\n识别结果：%1\n匹配分数：%2\n有效特征点：%3\n候选数量：%4")
                                    .arg(QString::fromStdString(matchedResult.boardId))
                                    .arg(QString::number(matchedResult.matchScore, 'f', 2))
                                    .arg(matchedResult.goodMatchCount)
                                    .arg(dbMatchedResults.size());
        ui->textOtherInfo->setText(ui->textOtherInfo->toPlainText() + summary);
    }
}

void BoardManager::onStartSelectedTest()
{
    if (!m_labelEditor) {
        return;
    }

    const int boardIndex = currentRepositoryIndex();
    if (boardIndex < 0 || boardIndex >= m_boards.size()) {
        QMessageBox::information(this, tr("测试"), tr("请选择板卡后再执行测试。"));
        return;
    }

    const QList<CompLabel> selected = m_labelEditor->selectedLabels();
    if (selected.isEmpty()) {
        QMessageBox::information(this, tr("测试"), tr("请先在图上选择元件（支持 Ctrl 多选）。"));
        return;
    }
    const QList<CompLabel> allLabels = m_labelEditor->currentLabels();

    const QString boardId = m_boards.at(boardIndex).boardId.trimmed().isEmpty()
        ? m_boards.at(boardIndex).name
        : m_boards.at(boardIndex).boardId;

    if (ui->textOtherInfo) {
        const QString current = ui->textOtherInfo->toPlainText();
        const QString selectedLine = QStringLiteral("已选元件：%1").arg(selected.size());
        if (!current.contains(QStringLiteral("已选元件："))) {
            ui->textOtherInfo->setText(current + QStringLiteral("\n") + selectedLine);
        } else {
            QString updated = current;
            updated.replace(QRegularExpression(QStringLiteral("已选元件：\\d+")), selectedLine);
            ui->textOtherInfo->setText(updated);
        }
    }

    const QString imagePath = resolveImagePath(m_boards.at(boardIndex).sourcePath);
    const QImage boardImage = imagePath.trimmed().isEmpty() ? QImage() : QImage(imagePath);

    emit testRequested(boardId, selected, allLabels, boardImage);
}

void BoardManager::populateBoardTable(const QString &filterText)
{
    if (!ui->tableBoards) {
        return;
    }

    m_rowToRepositoryIndex.clear();
    ui->tableBoards->setRowCount(0);
    const QString filter = filterText.trimmed();

    for (int index = 0; index < m_boards.size(); ++index) {
        const auto &board = m_boards.at(index);

        if (!filter.isEmpty()) {
            const bool matched = board.name.contains(filter, Qt::CaseInsensitive)
                || board.model.contains(filter, Qt::CaseInsensitive)
                || board.version.contains(filter, Qt::CaseInsensitive)
                || board.boardId.contains(filter, Qt::CaseInsensitive);
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
        m_rowToRepositoryIndex.push_back(index);
    }
}

int BoardManager::currentRepositoryIndex() const
{
    if (!ui->tableBoards) {
        return -1;
    }

    const int row = ui->tableBoards->currentRow();
    if (row < 0 || row >= m_rowToRepositoryIndex.size()) {
        return -1;
    }

    return m_rowToRepositoryIndex.at(row);
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
        ui->textOtherInfo->setText(QStringLiteral("BoardId：%1\n板卡名称：%2\n版本：%3\n数据库：%4")
                                       .arg(entry.boardId)
                                       .arg(entry.name)
                                       .arg(entry.version)
                                       .arg(m_databasePath));
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

    QDir dbDir(QFileInfo(m_databasePath).dir());
    const QString imageDir = dbDir.filePath(QStringLiteral("images"));
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

    cv::Mat featureBgr = sourceBgr;
    bool featureImageIsPcbCrop = false;

    QVector<CompLabel> labels;
    try {
        std::vector<CompLabel> results;
        if (useManualCrop && pcbRect.isValid() && !pcbRect.isNull()) {
            const cv::Mat cropBgr = qimageToBgrMat(pcbImage);
            if (cropBgr.empty()) {
                QMessageBox::warning(this, tr("新建板卡"), tr("裁剪图片格式不支持。"));
                return;
            }
            featureBgr = cropBgr;
            featureImageIsPcbCrop = true;
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
    QString boardId = modelEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::information(this, tr("新建板卡"), tr("请输入板卡名称。"));
        return;
    }
    if (boardId.isEmpty()) {
        boardId = name;
    }

    BoardRecord board;
    board.boardId = boardId;
    board.name = name;
    board.version = QStringLiteral("v1");
    board.createdAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    board.imagePath = savedPath;

    int nextId = 1;
    for (auto &label : labels) {
        if (label.id <= 0) {
            label.id = nextId;
        }
        nextId = std::max(nextId, label.id + 1);

        if (label.position_number.trimmed().isEmpty()) {
            label.position_number = QString::number(label.id);
        }

        BoardComponentRecord component;
        component.label = label;
        component.typeName = componentTypeNameByClassId(label.cls);
        component.model = label.label;
        board.components.append(component);
    }
    board.nextComponentIndex = nextId;

    QString errorMessage;
    if (!m_repository->addBoard(board, &errorMessage)) {
        QMessageBox::warning(this, tr("新建板卡"), tr("保存板卡失败: %1").arg(errorMessage));
        return;
    }

    if (!m_repository->save(&errorMessage)) {
        QMessageBox::warning(this, tr("新建板卡"), tr("写回数据库失败: %1").arg(errorMessage));
        return;
    }

    const std::vector<std::string> ids = {board.boardId.toStdString()};
    const std::vector<cv::Mat> images = {featureBgr};
    SIFT_MATCHER->appendToDatabase(ids, images, featureImageIsPcbCrop);

    loadBoardsFromStorage();
    populateBoardTable(ui->lineEditSearch ? ui->lineEditSearch->text() : QString());
}

void BoardManager::onDeleteBoard()
{
    const int boardIndex = currentRepositoryIndex();
    if (boardIndex < 0) {
        QMessageBox::information(this, tr("Delete Board"), tr("请选择要删除的板卡。"));
        return;
    }

    const BoardRecord *board = m_repository->boardAt(boardIndex);
    if (!board) {
        return;
    }

    const auto reply = QMessageBox::question(this,
                                             tr("删除板卡"),
                                             tr("确定删除板卡 %1 吗？").arg(board->boardId));
    if (reply != QMessageBox::Yes) {
        return;
    }

    SIFT_MATCHER->removeFromDatabase(board->boardId.toStdString());

    {
        TpsParamService tpsParamService;
        tpsParamService.setDatabasePath(QFileInfo(m_databasePath).dir().filePath(QStringLiteral("i_params_db.json")));
        QString paramsError;
        if (!tpsParamService.removeBoardParams(board->boardId, &paramsError)) {
            QMessageBox::warning(this, tr("删除板卡"), tr("删除参数数据库失败: %1").arg(paramsError));
            return;
        }
    }

    QString errorMessage;
    if (!m_repository->removeBoardAt(boardIndex, &errorMessage)) {
        QMessageBox::warning(this, tr("删除板卡"), errorMessage);
        return;
    }

    if (!m_repository->save(&errorMessage)) {
        QMessageBox::warning(this, tr("删除板卡"), errorMessage);
        return;
    }

    loadBoardsFromStorage();
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

void BoardManager::onBoardSelectionChanged()
{
    const int boardIndex = currentRepositoryIndex();
    if (boardIndex < 0 || boardIndex >= m_boards.size()) {
        return;
    }

    const auto &entry = m_boards.at(boardIndex);
    updateInfoPanel(entry);

    if (m_labelEditor) {
        m_loadingBoard = true;
        const QImage image = loadImageByDbPath(entry.sourcePath, m_databasePath);
        m_labelEditor->setImage(image);
        m_labelEditor->setLabels(QList<CompLabel>(entry.components.cbegin(), entry.components.cend()), true);
        m_labelEditor->setCurrentBoardId(entry.boardId);
        m_loadingBoard = false;
    }
}

void BoardManager::onLabelsChanged(const QList<CompLabel> &labels)
{
    if (m_loadingBoard) {
        return;
    }

    const int boardIndex = currentRepositoryIndex();
    const BoardRecord *current = m_repository->boardAt(boardIndex);
    if (!current) {
        return;
    }

    BoardRecord board = *current;
    board.components.clear();
    board.components.reserve(labels.size());

    int maxId = 0;
    for (CompLabel label : labels) {
        if (label.id <= 0) {
            label.id = ++maxId;
        }
        maxId = std::max(maxId, label.id);
        if (label.position_number.trimmed().isEmpty()) {
            label.position_number = QString::number(label.id);
        }

        BoardComponentRecord component;
        component.label = label;
        component.typeName = componentTypeNameByClassId(label.cls);
        component.model = label.label;
        board.components.append(component);
    }
    board.nextComponentIndex = std::max(board.nextComponentIndex, maxId + 1);

    QString errorMessage;
    if (!m_repository->updateBoard(boardIndex, board, &errorMessage)) {
        if (ui->textOtherInfo) {
            ui->textOtherInfo->setText(QStringLiteral("更新失败：%1").arg(errorMessage));
        }
        return;
    }

    if (!m_repository->save(&errorMessage)) {
        if (ui->textOtherInfo) {
            ui->textOtherInfo->setText(QStringLiteral("写回失败：%1").arg(errorMessage));
        }
        return;
    }

    if (boardIndex >= 0 && boardIndex < m_boards.size()) {
        m_boards[boardIndex] = buildEntryFromRepositoryBoard(boardIndex);
        updateInfoPanel(m_boards[boardIndex]);
    }
}
