#include "tasklogstatisticspage.h"
#include "ui_tasklogstatisticspage.h"
#include "../TaskTransport/tasklogtransportwidget.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlTableModel>
#include <QTableView>
#include <QUuid>

namespace {
void ensureSqliteDriverSearchPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString qtPluginRoot = QStringLiteral("D:/QT/6.7.3/msvc2022_64/plugins");
    const QStringList candidates = {
        appDir,
        qtPluginRoot
    };

    for (const QString &path : candidates) {
        if (path.trimmed().isEmpty()) {
            continue;
        }
        QDir dir(path);
        if (!dir.exists()) {
            continue;
        }
        if (!QCoreApplication::libraryPaths().contains(dir.absolutePath())) {
            QCoreApplication::addLibraryPath(dir.absolutePath());
        }
    }
}

QString sqlDatabaseErrorText(const QSqlDatabase &database)
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        return QStringLiteral("QSQLITE driver not loaded. Expected plugin under app sqldrivers/ or D:/QT/6.7.3/msvc2022_64/plugins");
    }

    const QString text = database.lastError().text().trimmed();
    return text.isEmpty() ? QStringLiteral("unknown database error") : text;
}

void applyTableTheme(QTableView *tableView)
{
    if (!tableView) {
        return;
    }

    const QString theme = qApp ? qApp->property("atsTheme").toString().toLower() : QString();
    if (theme == QStringLiteral("light")) {
        tableView->setStyleSheet(QStringLiteral(
            "QTableView {"
            "  background:#ffffff;"
            "  alternate-background-color:#f3f6fa;"
            "  color:#1f2937;"
            "  gridline-color:#d6dde6;"
            "  selection-background-color:#cfe8ff;"
            "  selection-color:#102a43;"
            "}"
            "QHeaderView::section {"
            "  background:#eef3f8;"
            "  color:#1f2937;"
            "  border:1px solid #d6dde6;"
            "  padding:4px 6px;"
            "}"));
        return;
    }

    tableView->setStyleSheet(QStringLiteral(
        "QTableView {"
        "  background:#111111;"
        "  alternate-background-color:#1b1b1b;"
        "  color:#f3f4f6;"
        "  gridline-color:#303030;"
        "  selection-background-color:#184a73;"
        "  selection-color:#ffffff;"
        "}"
        "QHeaderView::section {"
        "  background:#151515;"
        "  color:#f3f4f6;"
        "  border:1px solid #303030;"
        "  padding:4px 6px;"
        "}"));
}
}

TaskLogStatisticsPage::TaskLogStatisticsPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TaskLogStatisticsPage)
    , m_connectionName(QStringLiteral("task_log_stats_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
    ui->setupUi(this);
    ui->statusLabel->setMinimumWidth(220);

    m_transportWidget = new TaskLogTransportWidget(this);
    ui->transportTabLayout->addWidget(m_transportWidget);

    ui->tableView->setAlternatingRowColors(true);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView->setWordWrap(false);
    ui->tableView->verticalHeader()->setVisible(false);
    ui->tableView->horizontalHeader()->setHighlightSections(false);
    applyTableTheme(ui->tableView);

    connect(ui->refreshButton, &QPushButton::clicked, this, &TaskLogStatisticsPage::refresh);
    refresh();
}

TaskLogStatisticsPage::~TaskLogStatisticsPage()
{
    delete m_model;
    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
            if (database.isValid()) {
                database.close();
            }
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    delete ui;
}

void TaskLogStatisticsPage::refresh()
{
    ui->pathLabel->setText(QStringLiteral("数据库：%1").arg(QDir::toNativeSeparators(resolveDatabasePath())));

    QString errorMessage;
    if (!openDatabase(&errorMessage)) {
        updateStatusLabel(QStringLiteral("打开失败：%1").arg(errorMessage), true);
        return;
    }

    if (!m_model) {
        m_model = new QSqlTableModel(this, QSqlDatabase::database(m_connectionName));
        m_model->setTable(QStringLiteral("test_task_logs"));
        m_model->setEditStrategy(QSqlTableModel::OnManualSubmit);
        m_model->setHeaderData(1, Qt::Horizontal, QStringLiteral("任务ID"));
        m_model->setHeaderData(2, Qt::Horizontal, QStringLiteral("运行ID"));
        m_model->setHeaderData(3, Qt::Horizontal, QStringLiteral("测试时间"));
        m_model->setHeaderData(4, Qt::Horizontal, QStringLiteral("板卡名称"));
        m_model->setHeaderData(5, Qt::Horizontal, QStringLiteral("故障类型"));
        m_model->setHeaderData(6, Qt::Horizontal, QStringLiteral("状态"));
        m_model->setHeaderData(7, Qt::Horizontal, QStringLiteral("插件ID"));
        m_model->setHeaderData(8, Qt::Horizontal, QStringLiteral("元件位号"));
        m_model->setHeaderData(9, Qt::Horizontal, QStringLiteral("创建时间"));
        m_model->setHeaderData(10, Qt::Horizontal, QStringLiteral("结束时间"));
        ui->tableView->setModel(m_model);
        ui->tableView->setSortingEnabled(true);
        ui->tableView->sortByColumn(0, Qt::DescendingOrder);
        ui->tableView->hideColumn(0);
    }

    if (!m_model->select()) {
        updateStatusLabel(QStringLiteral("读取失败：%1").arg(m_model->lastError().text()), true);
        return;
    }

    ui->tableView->resizeColumnsToContents();
    ui->tableView->horizontalHeader()->setStretchLastSection(true);
    updateStatusLabel(QStringLiteral("共 %1 条记录").arg(m_model->rowCount()), false);
}

void TaskLogStatisticsPage::showStatisticsTab()
{
    if (ui && ui->tabWidget) {
        ui->tabWidget->setCurrentWidget(ui->statisticsTab);
    }
}

void TaskLogStatisticsPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!ui || !ui->tableView) {
        return;
    }

    if (event->type() == QEvent::StyleChange
        || event->type() == QEvent::PaletteChange
        || event->type() == QEvent::ApplicationPaletteChange) {
        applyTableTheme(ui->tableView);
    }
}

void TaskLogStatisticsPage::showTransportTab()
{
    if (ui && ui->tabWidget) {
        ui->tabWidget->setCurrentWidget(ui->transportTab);
    }
}

QString TaskLogStatisticsPage::resolveDatabasePath() const
{
    const QString baseDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime_logs"));
    return QDir(baseDir).filePath(QStringLiteral("test_task_logs.sqlite"));
}

bool TaskLogStatisticsPage::openDatabase(QString *errorMessage)
{
    const QFileInfo info(resolveDatabasePath());
    QDir dir(info.absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建目录：%1").arg(QDir::toNativeSeparators(info.absolutePath()));
        }
        return false;
    }

    ensureSqliteDriverSearchPath();

    QSqlDatabase database;
    if (QSqlDatabase::contains(m_connectionName)) {
        database = QSqlDatabase::database(m_connectionName);
    } else {
        database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        database.setDatabaseName(info.absoluteFilePath());
    }

    if (!database.isOpen() && !database.open()) {
        if (errorMessage) {
            *errorMessage = sqlDatabaseErrorText(database);
        }
        return false;
    }

    QSqlQuery query(database);
    const QString sql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS test_task_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "task_id TEXT NOT NULL,"
        "run_id TEXT,"
        "test_time TEXT NOT NULL,"
        "board_name TEXT,"
        "fault_type TEXT,"
        "status TEXT,"
        "plugin_id TEXT,"
        "component_ref TEXT,"
        "created_at TEXT,"
        "closed_at TEXT,"
        "UNIQUE(task_id, run_id)"
        ")");
    if (!query.exec(sql)) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    return true;
}

void TaskLogStatisticsPage::updateStatusLabel(const QString &message, bool isError)
{
    if (!ui || !ui->statusLabel) {
        return;
    }

    ui->statusLabel->setText(message);
    ui->statusLabel->setStyleSheet(isError
                                       ? QStringLiteral("color:#c0392b;")
                                       : QStringLiteral("color:#2e7d32;"));
}
