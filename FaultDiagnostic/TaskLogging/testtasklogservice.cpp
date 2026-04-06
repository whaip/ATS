#include "testtasklogservice.h"
#include "../TaskTransport/tasklogtransportservice.h"
#include "../Core/testtaskcontextmanager.h"
#include "../../logger.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QRegularExpression>
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

QString normalizedBoardName(const TaskContextRecord &record)
{
    const QString boardName = record.boardId.trimmed();
    return boardName.isEmpty() ? QStringLiteral("board") : boardName;
}

QString extractModeName(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return {};
    }

    const QRegularExpression pattern(QStringLiteral("MODE\\d+\\([^)]+\\)"));
    const QRegularExpressionMatch match = pattern.match(text);
    return match.hasMatch() ? match.captured(0).trimmed() : QString();
}

QString resolvedFaultType(const TaskContextRecord &record)
{
    const QString detailHtml = record.diagnosticSummary.value(QStringLiteral("detailHtml")).toString();
    const QString modeFromDetail = extractModeName(detailHtml);
    if (!modeFromDetail.isEmpty()) {
        return modeFromDetail;
    }

    const QString reportHtml = record.reportHtml;
    const QString modeFromReport = extractModeName(reportHtml);
    if (!modeFromReport.isEmpty()) {
        return modeFromReport;
    }

    const QString summary = record.diagnosticSummary.value(QStringLiteral("summary")).toString().trimmed();
    if (!summary.isEmpty()
        && summary.compare(QStringLiteral("PASS"), Qt::CaseInsensitive) != 0
        && summary.compare(QStringLiteral("FAIL"), Qt::CaseInsensitive) != 0) {
        return summary;
    }

    const QVariant predictedMode = record.diagnosticMetrics.value(QStringLiteral("mode.predicted"));
    if (predictedMode.isValid()) {
        return QStringLiteral("MODE%1").arg(predictedMode.toInt());
    }

    const QString reason = record.closeReason.trimmed();
    if (!reason.isEmpty()) {
        return reason;
    }

    const bool diagnosed = !record.diagnosticSummary.isEmpty();
    const bool success = record.diagnosticSummary.value(QStringLiteral("success")).toBool();
    if (diagnosed && success) {
        return QStringLiteral("Normal");
    }

    const QString status = record.status.trimmed();
    return status.isEmpty() ? QStringLiteral("Unknown") : status;
}

QString formattedTestTime(const TaskContextRecord &record)
{
    const QDateTime timestamp = record.closedAt.isValid() ? record.closedAt : record.createdAt;
    const QDateTime safeTimestamp = timestamp.isValid() ? timestamp : QDateTime::currentDateTime();
    return safeTimestamp.toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss"));
}
}

TestTaskLogService::TestTaskLogService(QObject *parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("task_log_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

TestTaskLogService::~TestTaskLogService()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
            if (database.isValid()) {
                database.close();
            }
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

void TestTaskLogService::setDatabasePath(const QString &filePath)
{
    m_databasePath = QDir::cleanPath(filePath);
}

QString TestTaskLogService::databasePath() const
{
    return resolveDatabasePath();
}

bool TestTaskLogService::appendTaskRecord(const TaskContextRecord &record, QString *errorMessage)
{
    if (!ensureDatabase(errorMessage)) {
        return false;
    }

    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    if (!database.isOpen() && !database.open()) {
        if (errorMessage) {
            *errorMessage = sqlDatabaseErrorText(database);
        }
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO test_task_logs ("
        "task_id, run_id, test_time, board_name, fault_type, status, plugin_id, component_ref, created_at, closed_at"
        ") VALUES ("
        ":task_id, :run_id, :test_time, :board_name, :fault_type, :status, :plugin_id, :component_ref, :created_at, :closed_at"
        ")"));
    query.bindValue(QStringLiteral(":task_id"), record.taskId);
    query.bindValue(QStringLiteral(":run_id"), record.runId);
    query.bindValue(QStringLiteral(":test_time"), formattedTestTime(record));
    query.bindValue(QStringLiteral(":board_name"), normalizedBoardName(record));
    query.bindValue(QStringLiteral(":fault_type"), resolvedFaultType(record));
    query.bindValue(QStringLiteral(":status"), record.status.trimmed());
    query.bindValue(QStringLiteral(":plugin_id"), record.pluginId.trimmed());
    query.bindValue(QStringLiteral(":component_ref"), record.componentRef.trimmed());
    query.bindValue(QStringLiteral(":created_at"),
                    record.createdAt.isValid() ? record.createdAt.toString(Qt::ISODate) : QString());
    query.bindValue(QStringLiteral(":closed_at"),
                    record.closedAt.isValid() ? record.closedAt.toString(Qt::ISODate) : QString());

    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    TaskLogTransportBroadcaster::instance()->notifyStatisticsChanged(resolveDatabasePath());

    return true;
}

QString TestTaskLogService::resolveDatabasePath() const
{
    if (!m_databasePath.trimmed().isEmpty()) {
        return m_databasePath;
    }

    const QString baseDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime_logs"));
    return QDir(baseDir).filePath(QStringLiteral("test_task_logs.sqlite"));
}

bool TestTaskLogService::ensureDatabase(QString *errorMessage)
{
    if (!ensureDirectory(errorMessage)) {
        return false;
    }

    ensureSqliteDriverSearchPath();

    QSqlDatabase database;
    if (QSqlDatabase::contains(m_connectionName)) {
        database = QSqlDatabase::database(m_connectionName);
    } else {
        database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        database.setDatabaseName(resolveDatabasePath());
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

bool TestTaskLogService::ensureDirectory(QString *errorMessage) const
{
    const QFileInfo info(resolveDatabasePath());
    const QString dirPath = info.absolutePath();
    QDir dir(dirPath);
    if (dir.exists()) {
        return true;
    }

    if (dir.mkpath(QStringLiteral("."))) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("cannot create log directory: %1").arg(dirPath);
    }
    return false;
}
