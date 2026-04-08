#include "runtimerecordspage.h"

#include "ui_runtimerecordspage.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableWidgetItem>

namespace {

QString safeValue(const QJsonObject &object, const QString &key)
{
    return object.value(key).toString().trimmed();
}

bool isPlaceholderBoardId(const QString &boardId)
{
    const QString normalized = boardId.trimmed().toLower();
    return normalized.isEmpty()
        || normalized == QStringLiteral("board")
        || normalized == QStringLiteral("unknown");
}

QString normalizedAbsolutePath(const QString &path)
{
    const QFileInfo info(path);
    const QString absolute = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    return QDir::cleanPath(absolute);
}

QColor mixColor(const QColor &first, const QColor &second, qreal ratio)
{
    const qreal clamped = qBound(0.0, ratio, 1.0);
    const qreal inverse = 1.0 - clamped;
    return QColor::fromRgbF(first.redF() * inverse + second.redF() * clamped,
                            first.greenF() * inverse + second.greenF() * clamped,
                            first.blueF() * inverse + second.blueF() * clamped,
                            first.alphaF() * inverse + second.alphaF() * clamped);
}

void updateCheckItemVisual(QTableWidgetItem *item)
{
    if (!item) {
        return;
    }

    const QPalette palette = qApp->palette();
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);
    const QColor highlight = palette.color(QPalette::Highlight);
    const QColor highlightText = palette.color(QPalette::HighlightedText);
    const QColor button = palette.color(QPalette::Button);

    item->setTextAlignment(Qt::AlignCenter);
    if (item->checkState() == Qt::Checked) {
        item->setText(QStringLiteral("已选"));
        item->setBackground(mixColor(highlight, base, 0.72));
        item->setForeground(mixColor(highlightText, text, 0.2));
    } else {
        item->setText(QStringLiteral("未选"));
        item->setBackground(mixColor(button, base, 0.55));
        item->setForeground(mixColor(text, base, 0.35));
    }
}

QMap<QString, QString> loadBoardNamesByRunId()
{
    QMap<QString, QString> result;
    const QString dbPath = QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("runtime_logs/test_task_logs.sqlite"));
    if (!QFileInfo::exists(dbPath)) {
        return result;
    }

    const QString connectionName = QStringLiteral("runtime_records_%1")
                                       .arg(reinterpret_cast<quintptr>(&result));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(dbPath);
        if (!database.open()) {
            return result;
        }

        QSqlQuery query(database);
        if (query.exec(QStringLiteral("SELECT run_id, board_name FROM test_task_logs"))) {
            while (query.next()) {
                const QString runId = query.value(0).toString().trimmed();
                const QString boardName = query.value(1).toString().trimmed();
                if (!runId.isEmpty() && !boardName.isEmpty() && !isPlaceholderBoardId(boardName)) {
                    result.insert(runId, boardName);
                }
            }
        }

        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

} // namespace

RuntimeRecordsPage::RuntimeRecordsPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::RuntimeRecordsPage)
{
    ui->setupUi(this);
    setupTable();
    applyTheme();

    connect(ui->applyFilterButton, &QPushButton::clicked, this, &RuntimeRecordsPage::rebuildTable);
    connect(ui->refreshButton, &QPushButton::clicked, this, &RuntimeRecordsPage::refresh);
    connect(ui->taskIdEdit, &QLineEdit::returnPressed, this, &RuntimeRecordsPage::rebuildTable);
    connect(ui->runIdEdit, &QLineEdit::returnPressed, this, &RuntimeRecordsPage::rebuildTable);
    connect(ui->boardIdEdit, &QLineEdit::returnPressed, this, &RuntimeRecordsPage::rebuildTable);
    connect(ui->beforeDateCheck, &QCheckBox::toggled, this, &RuntimeRecordsPage::rebuildTable);
    connect(ui->beforeDateEdit, &QDateEdit::dateChanged, this, [this](const QDate &) {
        if (ui->beforeDateCheck->isChecked()) {
            rebuildTable();
        }
    });
    connect(ui->table, &QTableWidget::currentCellChanged, this, [this](int, int, int, int) {
        updateDetailViews();
    });
    connect(ui->table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!item || item->column() != 0) {
            return;
        }
        updateCheckItemVisual(item);
    });

    connect(ui->selectFilteredButton, &QPushButton::clicked, this, [this]() {
        for (int row = 0; row < ui->table->rowCount(); ++row) {
            if (auto *item = ui->table->item(row, 0)) {
                item->setCheckState(Qt::Checked);
            }
        }
    });

    connect(ui->clearSelectionButton, &QPushButton::clicked, this, [this]() {
        for (int row = 0; row < ui->table->rowCount(); ++row) {
            if (auto *item = ui->table->item(row, 0)) {
                item->setCheckState(Qt::Unchecked);
            }
        }
    });

    connect(ui->deleteSelectedButton, &QPushButton::clicked, this, [this]() {
        QVector<int> recordIndexes;
        for (int row = 0; row < ui->table->rowCount(); ++row) {
            auto *checkItem = ui->table->item(row, 0);
            if (!checkItem || checkItem->checkState() != Qt::Checked) {
                continue;
            }
            if (row >= 0 && row < m_visibleIndexes.size()) {
                recordIndexes.push_back(m_visibleIndexes.at(row));
            }
        }

        if (recordIndexes.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("删除记录"), QStringLiteral("请先勾选要删除的运行记录。"));
            return;
        }

        const QString message = QStringLiteral("将删除 %1 条运行记录对应的任务快照和运行日志，是否继续？")
                                    .arg(recordIndexes.size());
        if (QMessageBox::question(this, QStringLiteral("确认删除"), message) != QMessageBox::Yes) {
            return;
        }

        QStringList failures;
        for (int index : recordIndexes) {
            if (index < 0 || index >= m_records.size()) {
                continue;
            }
            QString errorMessage;
            if (!removeRecordArtifacts(m_records.at(index), &errorMessage)) {
                failures << errorMessage;
            }
        }

        refresh();

        if (failures.isEmpty()) {
            QMessageBox::information(this, QStringLiteral("删除完成"), QStringLiteral("选中的运行记录已删除。"));
        } else {
            QMessageBox::warning(this, QStringLiteral("部分删除失败"), failures.join(QStringLiteral("\n")));
        }
    });

    refresh();
}

RuntimeRecordsPage::~RuntimeRecordsPage()
{
    delete ui;
}

void RuntimeRecordsPage::refresh()
{
    loadRecords();
    rebuildTable();
}

void RuntimeRecordsPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event) {
        return;
    }

    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange
        || event->type() == QEvent::StyleChange) {
        applyTheme();
    }
}

QString RuntimeRecordsPage::resolveRuntimeLogsPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime_logs"));
}

QString RuntimeRecordsPage::resolveRuntimeTasksPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime_tasks"));
}

void RuntimeRecordsPage::applyTheme()
{
    const QPalette palette = this->palette();
    const QColor highlight = palette.color(QPalette::Highlight);
    const QColor highlightedText = palette.color(QPalette::HighlightedText);
    const QColor inactiveBackground = mixColor(highlight, palette.color(QPalette::Base), 0.45);
    const QColor inactiveText = mixColor(highlightedText, palette.color(QPalette::Text), 0.25);

    ui->table->setStyleSheet(QStringLiteral(
        "QTableWidget::item:selected {"
        " background-color: %1;"
        " color: %2;"
        "}"
        "QTableWidget::item:selected:active {"
        " background-color: %1;"
        " color: %2;"
        "}"
        "QTableWidget::item:selected:!active {"
        " background-color: %3;"
        " color: %4;"
        "}"
    ).arg(highlight.name(),
          highlightedText.name(),
          inactiveBackground.name(),
          inactiveText.name()));

    for (int row = 0; row < ui->table->rowCount(); ++row) {
        updateCheckItemVisual(ui->table->item(row, 0));
    }
}

void RuntimeRecordsPage::setupTable()
{
    ui->table->setColumnCount(10);
    ui->table->setHorizontalHeaderLabels({
        QStringLiteral("选择"),
        QStringLiteral("任务ID"),
        QStringLiteral("运行ID"),
        QStringLiteral("板卡"),
        QStringLiteral("位号"),
        QStringLiteral("插件"),
        QStringLiteral("创建时间"),
        QStringLiteral("结束时间"),
        QStringLiteral("任务快照"),
        QStringLiteral("运行日志")
    });
    ui->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->table->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->table->setAlternatingRowColors(true);
    ui->table->verticalHeader()->setVisible(false);
    ui->table->horizontalHeader()->setStretchLastSection(true);
    ui->table->setColumnWidth(0, 64);
    ui->table->setColumnWidth(1, 120);
    ui->table->setColumnWidth(2, 180);
    ui->table->setColumnWidth(3, 120);
    ui->table->setColumnWidth(4, 80);
    ui->table->setColumnWidth(5, 160);
    ui->table->setColumnWidth(6, 150);
    ui->table->setColumnWidth(7, 150);
    ui->table->setColumnWidth(8, 80);
    ui->table->setColumnWidth(9, 80);

    ui->taskBrowser->setMarkdown(QStringLiteral("# 任务快照\n\n请选择一条记录查看任务快照。"));
    ui->logBrowser->setMarkdown(QStringLiteral("# 运行日志\n\n请选择一条记录查看运行日志。"));
    ui->verticalSplitter->setStretchFactor(0, 3);
    ui->verticalSplitter->setStretchFactor(1, 2);
    ui->detailSplitter->setStretchFactor(0, 1);
    ui->detailSplitter->setStretchFactor(1, 1);
}

void RuntimeRecordsPage::loadRecords()
{
    m_records.clear();
    const QMap<QString, QString> boardNamesByRunId = loadBoardNamesByRunId();

    const QDir tasksDir(resolveRuntimeTasksPath());
    QMap<QString, int> runIdToRecordIndex;

    const QFileInfoList taskFiles = tasksDir.entryInfoList({QStringLiteral("*.json")},
                                                           QDir::Files,
                                                           QDir::Time | QDir::Reversed);
    for (const QFileInfo &fileInfo : taskFiles) {
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject()) {
            continue;
        }

        const QJsonObject object = document.object();
        RuntimeRecord record;
        record.taskId = safeValue(object, QStringLiteral("taskId"));
        record.runId = safeValue(object, QStringLiteral("runId"));
        record.boardId = safeValue(object, QStringLiteral("boardId"));
        record.componentRef = safeValue(object, QStringLiteral("componentRef"));
        record.pluginId = safeValue(object, QStringLiteral("pluginId"));
        record.createdAt = safeValue(object, QStringLiteral("createdAt"));
        record.closedAt = safeValue(object, QStringLiteral("closedAt"));
        record.taskFilePath = fileInfo.absoluteFilePath();
        record.taskExists = true;
        record.createdDate = QDate::fromString(record.createdAt.left(10), Qt::ISODate);

        if (record.taskId.isEmpty()) {
            record.taskId = fileInfo.completeBaseName();
        }
        if (isPlaceholderBoardId(record.boardId) && boardNamesByRunId.contains(record.runId)) {
            record.boardId = boardNamesByRunId.value(record.runId);
        }

        m_records.push_back(record);
        if (!record.runId.isEmpty()) {
            runIdToRecordIndex.insert(record.runId, m_records.size() - 1);
        }
    }

    const QDir logsDir(resolveRuntimeLogsPath());
    const QFileInfoList logDirs = logsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                        QDir::Time | QDir::Reversed);
    for (const QFileInfo &dirInfo : logDirs) {
        const QString dirName = dirInfo.fileName();
        QString boardId = dirName;
        QString runId;

        const int last = dirName.lastIndexOf(QLatin1Char('_'));
        const int second = last > 0 ? dirName.lastIndexOf(QLatin1Char('_'), last - 1) : -1;
        const int third = second > 0 ? dirName.lastIndexOf(QLatin1Char('_'), second - 1) : -1;
        if (third > 0) {
            boardId = dirName.left(third);
            runId = dirName.mid(third + 1);
        }

        const QString runtimeJsonl = QDir(dirInfo.absoluteFilePath()).filePath(QStringLiteral("runtime.jsonl"));

        if (!runId.isEmpty() && runIdToRecordIndex.contains(runId)) {
            RuntimeRecord &record = m_records[runIdToRecordIndex.value(runId)];
            record.logDirPath = dirInfo.absoluteFilePath();
            record.logFilePath = runtimeJsonl;
            record.logExists = QFileInfo::exists(runtimeJsonl);
            if (isPlaceholderBoardId(record.boardId) && boardNamesByRunId.contains(runId)) {
                record.boardId = boardNamesByRunId.value(runId);
            } else if (isPlaceholderBoardId(record.boardId) && !boardId.trimmed().isEmpty()) {
                record.boardId = boardId;
            }
            continue;
        }

        RuntimeRecord record;
        record.taskId = QStringLiteral("(无任务快照)");
        record.runId = runId.isEmpty() ? dirName : runId;
        record.boardId = boardNamesByRunId.value(record.runId, boardId);
        record.logDirPath = dirInfo.absoluteFilePath();
        record.logFilePath = runtimeJsonl;
        record.logExists = QFileInfo::exists(runtimeJsonl);
        m_records.push_back(record);
    }
}

QVector<int> RuntimeRecordsPage::filteredRecordIndexes() const
{
    QVector<int> indexes;

    const QString taskIdFilter = ui->taskIdEdit->text().trimmed();
    const QString runIdFilter = ui->runIdEdit->text().trimmed();
    const QString boardIdFilter = ui->boardIdEdit->text().trimmed();
    const bool useBeforeDate = ui->beforeDateCheck->isChecked();
    const QDate beforeDate = ui->beforeDateEdit->date();

    for (int i = 0; i < m_records.size(); ++i) {
        const RuntimeRecord &record = m_records.at(i);

        if (!taskIdFilter.isEmpty() && !record.taskId.contains(taskIdFilter, Qt::CaseInsensitive)) {
            continue;
        }
        if (!runIdFilter.isEmpty() && !record.runId.contains(runIdFilter, Qt::CaseInsensitive)) {
            continue;
        }
        if (!boardIdFilter.isEmpty() && !record.boardId.contains(boardIdFilter, Qt::CaseInsensitive)) {
            continue;
        }
        if (useBeforeDate) {
            if (!record.createdDate.isValid() || !(record.createdDate < beforeDate)) {
                continue;
            }
        }

        indexes.push_back(i);
    }

    return indexes;
}

void RuntimeRecordsPage::rebuildTable()
{
    m_visibleIndexes = filteredRecordIndexes();
    ui->table->setRowCount(0);

    for (int row = 0; row < m_visibleIndexes.size(); ++row) {
        const RuntimeRecord &record = m_records.at(m_visibleIndexes.at(row));
        ui->table->insertRow(row);

        auto *checkItem = new QTableWidgetItem();
        checkItem->setFlags((checkItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable)
                            & ~Qt::ItemIsEditable);
        checkItem->setCheckState(Qt::Unchecked);
        updateCheckItemVisual(checkItem);

        ui->table->setItem(row, 0, checkItem);
        ui->table->setItem(row, 1, new QTableWidgetItem(record.taskId));
        ui->table->setItem(row, 2, new QTableWidgetItem(record.runId));
        ui->table->setItem(row, 3, new QTableWidgetItem(record.boardId));
        ui->table->setItem(row, 4, new QTableWidgetItem(record.componentRef));
        ui->table->setItem(row, 5, new QTableWidgetItem(record.pluginId));
        ui->table->setItem(row, 6, new QTableWidgetItem(record.createdAt));
        ui->table->setItem(row, 7, new QTableWidgetItem(record.closedAt));
        ui->table->setItem(row, 8, new QTableWidgetItem(record.taskExists ? QStringLiteral("有") : QStringLiteral("无")));
        ui->table->setItem(row, 9, new QTableWidgetItem(record.logExists ? QStringLiteral("有") : QStringLiteral("无")));
    }

    if (ui->table->rowCount() > 0) {
        ui->table->selectRow(0);
    } else {
        ui->taskBrowser->setMarkdown(QStringLiteral("# 任务快照\n\n当前没有匹配的任务快照记录。"));
        ui->logBrowser->setMarkdown(QStringLiteral("# 运行日志\n\n当前没有匹配的运行日志记录。"));
    }

    updateSummaryLabel();
    updateDetailViews();
}

void RuntimeRecordsPage::updateDetailViews()
{
    const int row = ui->table->currentRow();
    if (row < 0 || row >= m_visibleIndexes.size()) {
        ui->taskBrowser->setMarkdown(QStringLiteral("# 任务快照\n\n请选择一条记录查看任务快照。"));
        ui->logBrowser->setMarkdown(QStringLiteral("# 运行日志\n\n请选择一条记录查看运行日志。"));
        return;
    }

    const RuntimeRecord &record = m_records.at(m_visibleIndexes.at(row));

    if (record.taskExists) {
        ui->taskBrowser->setPlainText(readTextFile(record.taskFilePath));
    } else {
        ui->taskBrowser->setMarkdown(QStringLiteral("# 任务快照\n\n该记录未找到对应的任务快照文件。"));
    }

    if (record.logExists) {
        ui->logBrowser->setPlainText(readTextFile(record.logFilePath));
    } else {
        ui->logBrowser->setMarkdown(QStringLiteral("# 运行日志\n\n该记录未找到对应的运行日志文件。"));
    }
}

void RuntimeRecordsPage::updateSummaryLabel()
{
    ui->summaryLabel->setText(QStringLiteral("共 %1 条记录，当前筛选结果 %2 条。")
                                  .arg(m_records.size())
                                  .arg(m_visibleIndexes.size()));
}

QString RuntimeRecordsPage::readTextFile(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("读取文件失败：%1").arg(QDir::toNativeSeparators(path));
    }
    return QString::fromUtf8(file.readAll());
}

bool RuntimeRecordsPage::pathWithinRoot(const QString &path, const QString &rootPath) const
{
    const QString normalizedPath = normalizedAbsolutePath(path);
    const QString normalizedRoot = normalizedAbsolutePath(rootPath);
    if (normalizedPath.isEmpty() || normalizedRoot.isEmpty()) {
        return false;
    }

    QDir rootDir(normalizedRoot);
    const QString relativePath = QDir::cleanPath(rootDir.relativeFilePath(normalizedPath));
    if (relativePath.isEmpty() || relativePath == QStringLiteral(".")) {
        return true;
    }
    if (QDir::isAbsolutePath(relativePath)) {
        return false;
    }
    return !relativePath.startsWith(QStringLiteral(".."));
}

bool RuntimeRecordsPage::removeRecordArtifacts(const RuntimeRecord &record, QString *errorMessage) const
{
    const QString tasksRoot = resolveRuntimeTasksPath();
    const QString logsRoot = resolveRuntimeLogsPath();

    if (record.taskExists && !record.taskFilePath.isEmpty()) {
        if (!pathWithinRoot(record.taskFilePath, tasksRoot)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("任务文件路径越界：%1")
                                    .arg(QDir::toNativeSeparators(record.taskFilePath));
            }
            return false;
        }

        if (QFileInfo::exists(record.taskFilePath) && !QFile::remove(record.taskFilePath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("删除任务文件失败：%1")
                                    .arg(QDir::toNativeSeparators(record.taskFilePath));
            }
            return false;
        }
    }

    if (!record.logDirPath.isEmpty()) {
        if (!pathWithinRoot(record.logDirPath, logsRoot)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("日志目录路径越界：%1")
                                    .arg(QDir::toNativeSeparators(record.logDirPath));
            }
            return false;
        }

        QDir logDir(record.logDirPath);
        if (logDir.exists() && !logDir.removeRecursively()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("删除日志目录失败：%1")
                                    .arg(QDir::toNativeSeparators(record.logDirPath));
            }
            return false;
        }
    }

    return true;
}
