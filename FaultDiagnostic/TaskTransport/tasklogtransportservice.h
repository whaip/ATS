#ifndef TASKLOGTRANSPORTSERVICE_H
#define TASKLOGTRANSPORTSERVICE_H

#include "tasklogtransportsettings.h"

#include <QByteArray>
#include <QObject>

struct TaskContextRecord;
class QTimer;
class QThread;
class TaskLogTransportWorker;

class TaskLogTransportService
{
public:
    static QByteArray buildStatisticsPayload(const QString &databasePath, QString *errorMessage = nullptr);
    static QByteArray buildTestPayload();

    static bool sendStatistics(const QString &databasePath, QString *errorMessage = nullptr);
    static bool sendPayload(const TaskLogTransportSettings &settings,
                            const QByteArray &payload,
                            QString *errorMessage = nullptr);
    static TaskLogTransportSettings listenerTargetSettings(const TaskLogTransportSettings &settings);
};

class TaskLogTransportBroadcaster : public QObject
{
public:
    static TaskLogTransportBroadcaster *instance();

    void start();
    void setDatabasePath(const QString &databasePath);
    void notifyStatisticsChanged(const QString &databasePath = QString());

private:
    explicit TaskLogTransportBroadcaster(QObject *parent = nullptr);

    void sendNow();
    QString resolveDatabasePath() const;
    bool shouldSuppressPeriodicError(const QString &errorMessage) const;

    QString m_databasePath;
    QThread *m_thread = nullptr;
    TaskLogTransportWorker *m_worker = nullptr;
};

#endif // TASKLOGTRANSPORTSERVICE_H
