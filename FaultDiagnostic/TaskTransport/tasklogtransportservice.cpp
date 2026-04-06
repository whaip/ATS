#include "tasklogtransportservice.h"

#include "../../logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>
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

QString socketErrorText(const QString &fallback, const QString &detail)
{
    return detail.trimmed().isEmpty() ? fallback : detail.trimmed();
}

bool resolveUdpAddress(const QString &host, QHostAddress *address, QString *errorMessage)
{
    if (!address) {
        return false;
    }

    QHostAddress parsedAddress;
    if (parsedAddress.setAddress(host)) {
        *address = parsedAddress;
        return true;
    }

    const QHostInfo info = QHostInfo::fromName(host);
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
        if (errorMessage) {
            *errorMessage = info.errorString().trimmed().isEmpty()
                                ? QStringLiteral("unable to resolve host")
                                : info.errorString().trimmed();
        }
        return false;
    }

    *address = info.addresses().first();
    return true;
}

QString recordTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString normalizedFaultName(const QString &faultType)
{
    return faultType.trimmed();
}

QString defaultDatabasePath()
{
    const QString baseDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime_logs"));
    return QDir(baseDir).filePath(QStringLiteral("test_task_logs.sqlite"));
}

QString normalizedErrorKey(const QString &errorMessage)
{
    return errorMessage.trimmed().toLower();
}
}

class TaskLogTransportWorker : public QObject
{
public:
    void setSettings(const TaskLogTransportSettings &settings)
    {
        m_settings = settings;
    }

    void startLoop(int intervalMs)
    {
        if (!m_timer) {
            m_timer = new QTimer(this);
            m_timer->setSingleShot(false);
            QObject::connect(m_timer, &QTimer::timeout, this, [this]() {
                sendNow();
            });
        }

        const int safeInterval = intervalMs > 0 ? intervalMs : 1000;
        m_timer->start(safeInterval);
        sendNow();
    }

    void setDatabasePath(const QString &databasePath)
    {
        if (!databasePath.trimmed().isEmpty()) {
            m_databasePath = QDir::cleanPath(databasePath);
        }
    }

    void notifyStatisticsChanged(const QString &databasePath)
    {
        if (!databasePath.trimmed().isEmpty()) {
            setDatabasePath(databasePath);
        }
        sendNow();
    }

private:
    void sendNow()
    {
        const QString databasePath = m_databasePath.trimmed().isEmpty() ? defaultDatabasePath() : m_databasePath;
        if (!QFileInfo::exists(databasePath)) {
            return;
        }

        QString errorMessage;
        if (TaskLogTransportService::sendStatistics(databasePath, m_settings, &errorMessage)) {
            m_lastLoggedError.clear();
            return;
        }

        if (errorMessage.trimmed().isEmpty() || shouldSuppressPeriodicError(errorMessage)) {
            return;
        }

        const QString errorKey = normalizedErrorKey(errorMessage);
        if (errorKey == m_lastLoggedError) {
            return;
        }

        m_lastLoggedError = errorKey;
        Logger::log(QStringLiteral("Task log transport periodic send failed: %1").arg(errorMessage),
                    Logger::Level::Warn);
    }

    bool shouldSuppressPeriodicError(const QString &errorMessage) const
    {
        const QString normalized = normalizedErrorKey(errorMessage);
        return normalized.contains(QStringLiteral("timed out"))
            || normalized.contains(QStringLiteral("connection refused"))
            || normalized.contains(QStringLiteral("actively refused"))
            || normalized.contains(QStringLiteral("host unreachable"))
            || normalized.contains(QStringLiteral("network unreachable"))
            || normalized.contains(QStringLiteral("connection timed out"));
    }

    QString m_databasePath;
    QString m_lastLoggedError;
    QTimer *m_timer = nullptr;
    TaskLogTransportSettings m_settings;
};

QByteArray TaskLogTransportService::buildStatisticsPayload(const QString &databasePath, QString *errorMessage)
{
    const QString connectionName =
        QStringLiteral("task_transport_stats_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    ensureSqliteDriverSearchPath();

    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(databasePath);
    const auto cleanup = [&database, &connectionName]() {
        if (database.isValid()) {
            database.close();
            database = QSqlDatabase();
        }
        QSqlDatabase::removeDatabase(connectionName);
    };

    if (!database.open()) {
        if (errorMessage) {
            *errorMessage = database.lastError().text().trimmed();
        }
        cleanup();
        return {};
    }

    int totalDetectedBoards = 0;
    QSqlQuery totalQuery(database);
    totalQuery.prepare(QStringLiteral(
        "SELECT COUNT(DISTINCT CASE "
        "WHEN IFNULL(TRIM(run_id), '') = '' THEN task_id "
        "ELSE run_id END) "
        "FROM test_task_logs"));
    if (!totalQuery.exec() || !totalQuery.next()) {
        if (errorMessage) {
            *errorMessage = totalQuery.lastError().text().trimmed();
        }
        cleanup();
        return {};
    }
    totalDetectedBoards = totalQuery.value(0).toInt();

    QJsonArray records;
    QSqlQuery recordsQuery(database);
    recordsQuery.prepare(QStringLiteral(
        "SELECT fault_type, COUNT(DISTINCT CASE "
        "WHEN IFNULL(TRIM(run_id), '') = '' THEN task_id "
        "ELSE run_id END) AS board_count "
        "FROM test_task_logs "
        "WHERE IFNULL(TRIM(fault_type), '') <> '' "
        "AND LOWER(TRIM(fault_type)) <> 'normal' "
        "AND LOWER(TRIM(fault_type)) <> 'pass' "
        "GROUP BY fault_type "
        "ORDER BY board_count DESC, fault_type ASC"));
    if (!recordsQuery.exec()) {
        if (errorMessage) {
            *errorMessage = recordsQuery.lastError().text().trimmed();
        }
        cleanup();
        return {};
    }

    while (recordsQuery.next()) {
        const QString faultName = normalizedFaultName(recordsQuery.value(0).toString());
        if (faultName.isEmpty()) {
            continue;
        }

        QJsonObject item;
        item.insert(QStringLiteral("name"), faultName);
        item.insert(QStringLiteral("count"), recordsQuery.value(1).toInt());
        records.append(item);
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("detectionDeviceName"), QString::fromUtf8(u8"电子板卡测试系统"));
    payload.insert(QStringLiteral("totalDetectedBoards"), totalDetectedBoards);
    payload.insert(QStringLiteral("records"), records);
    payload.insert(QStringLiteral("recordTime"), recordTimestamp());

    cleanup();
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

QByteArray TaskLogTransportService::buildTestPayload()
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("taskLogTransportTest"));
    object.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODate));
    object.insert(QStringLiteral("message"), QStringLiteral("Task log transport connectivity test"));
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool TaskLogTransportService::sendStatistics(const QString &databasePath, QString *errorMessage)
{
    return sendStatistics(databasePath, loadTaskLogTransportSettings(), errorMessage);
}

bool TaskLogTransportService::sendStatistics(const QString &databasePath,
                                             const TaskLogTransportSettings &settings,
                                             QString *errorMessage)
{
    if (!settings.sendEnabled) {
        return true;
    }

    const QByteArray payload = buildStatisticsPayload(databasePath, errorMessage);
    if (payload.isEmpty()) {
        return false;
    }
    return sendPayload(settings, payload, errorMessage);
}

bool TaskLogTransportService::sendPayload(const TaskLogTransportSettings &settings,
                                          const QByteArray &payload,
                                          QString *errorMessage)
{
    if (settings.sendHost.trimmed().isEmpty() || settings.sendPort == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("send target is incomplete");
        }
        return false;
    }

    if (settings.sendProtocol == TaskLogTransportProtocol::Udp) {
        QHostAddress targetAddress;
        if (!resolveUdpAddress(settings.sendHost.trimmed(), &targetAddress, errorMessage)) {
            return false;
        }

        QUdpSocket socket;
        const qint64 written = socket.writeDatagram(payload, targetAddress, settings.sendPort);
        if (written < 0 || written != payload.size()) {
            if (errorMessage) {
                *errorMessage = socketErrorText(QStringLiteral("udp send failed"), socket.errorString());
            }
            return false;
        }
        return true;
    }

    QTcpSocket socket;
    socket.connectToHost(settings.sendHost, settings.sendPort);
    if (!socket.waitForConnected(500)) {
        if (errorMessage) {
            *errorMessage = socketErrorText(QStringLiteral("tcp connect failed"), socket.errorString());
        }
        return false;
    }

    QByteArray bytes = payload;
    if (settings.sendProtocol == TaskLogTransportProtocol::Http) {
        const QByteArray host = settings.sendHost.toUtf8();
        bytes =
            "POST /task-statistics HTTP/1.1\r\n"
            "Host: " + host + ":" + QByteArray::number(settings.sendPort) + "\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + QByteArray::number(payload.size()) + "\r\n"
            "Connection: close\r\n\r\n" + payload;
    }

    if (socket.write(bytes) != bytes.size()) {
        if (errorMessage) {
            *errorMessage = socketErrorText(QStringLiteral("socket write failed"), socket.errorString());
        }
        return false;
    }
    if (!socket.waitForBytesWritten(500)) {
        if (errorMessage) {
            *errorMessage = socketErrorText(QStringLiteral("socket flush failed"), socket.errorString());
        }
        return false;
    }

    if (settings.sendProtocol == TaskLogTransportProtocol::Http) {
        socket.waitForReadyRead(500);
    }

    socket.disconnectFromHost();
    return true;
}

TaskLogTransportSettings TaskLogTransportService::listenerTargetSettings(const TaskLogTransportSettings &settings)
{
    TaskLogTransportSettings target = settings;
    target.sendEnabled = true;
    target.sendProtocol = settings.testProtocol;
    target.sendHost = settings.listenAddress.trimmed();
    target.sendPort = settings.listenPort;
    return target;
}

TaskLogTransportBroadcaster *TaskLogTransportBroadcaster::instance()
{
    static TaskLogTransportBroadcaster *broadcaster = new TaskLogTransportBroadcaster(QCoreApplication::instance());
    return broadcaster;
}

TaskLogTransportBroadcaster::TaskLogTransportBroadcaster(QObject *parent)
    : QObject(parent)
{
    m_settings = loadTaskLogTransportSettings();
}

void TaskLogTransportBroadcaster::start()
{
    if (m_thread && m_worker) {
        return;
    }

    const int intervalMs = m_settings.broadcastIntervalMs > 0 ? m_settings.broadcastIntervalMs : 1000;

    m_thread = new QThread(this);
    m_worker = new TaskLogTransportWorker();
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
        if (!m_thread) {
            return;
        }
        m_thread->quit();
        m_thread->wait(1500);
    });

    m_thread->start();

    const QString databasePath = resolveDatabasePath();
    QMetaObject::invokeMethod(m_worker, [this, intervalMs, databasePath]() {
        if (!m_worker) {
            return;
        }
        m_worker->setSettings(m_settings);
        m_worker->setDatabasePath(databasePath);
        m_worker->startLoop(intervalMs);
    }, Qt::QueuedConnection);
}

void TaskLogTransportBroadcaster::setDatabasePath(const QString &databasePath)
{
    if (!databasePath.trimmed().isEmpty()) {
        m_databasePath = QDir::cleanPath(databasePath);
    }
}

void TaskLogTransportBroadcaster::applySettings(const TaskLogTransportSettings &settings, bool sendImmediately)
{
    m_settings = settings;

    if (!m_worker) {
        return;
    }

    QMetaObject::invokeMethod(m_worker, [this, settings, sendImmediately]() {
        if (!m_worker) {
            return;
        }
        m_worker->setSettings(settings);
        if (sendImmediately) {
            m_worker->notifyStatisticsChanged(resolveDatabasePath());
        }
    }, Qt::QueuedConnection);
}

void TaskLogTransportBroadcaster::notifyStatisticsChanged(const QString &databasePath)
{
    if (!databasePath.trimmed().isEmpty()) {
        setDatabasePath(databasePath);
    }

    if (!m_worker) {
        return;
    }

    const QString resolvedPath = resolveDatabasePath();
    QMetaObject::invokeMethod(m_worker, [this, resolvedPath]() {
        if (m_worker) {
            m_worker->notifyStatisticsChanged(resolvedPath);
        }
    }, Qt::QueuedConnection);
}

void TaskLogTransportBroadcaster::sendNow()
{
    notifyStatisticsChanged(resolveDatabasePath());
}

QString TaskLogTransportBroadcaster::resolveDatabasePath() const
{
    if (!m_databasePath.trimmed().isEmpty()) {
        return m_databasePath;
    }
    return defaultDatabasePath();
}

bool TaskLogTransportBroadcaster::shouldSuppressPeriodicError(const QString &errorMessage) const
{
    Q_UNUSED(errorMessage);
    return false;
}
