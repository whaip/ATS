#ifndef TESTTASKCONTEXTMANAGER_H
#define TESTTASKCONTEXTMANAGER_H

#include <QObject>
#include <QDateTime>
#include <QMap>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include "../Diagnostics/diagnosticdatatypes.h"
#include "../TPS/Core/tpsmodels.h"

struct TaskPortHandle
{
    QString taskId;
    QString identifier;
    QString type;
    QString deviceKind;
    int channel = -1;
    int slot = -1;
    QString resourceId;
    bool occupied = true;
};

struct TaskContextRecord
{
    QString taskId;
    QString runId;
    QString boardId;
    QString componentRef;
    QString pluginId;

    QDateTime createdAt;
    QDateTime closedAt;
    QString status;
    QString closeReason;

    QJsonArray parameterSchema;
    QMap<QString, QVariant> parameterSnapshot;

    QVector<TaskPortHandle> portHandles;
    QJsonArray wiringGuide;
    QStringList roiBindings;

    QJsonObject deviceConfig;
    QJsonObject rawDataSummary;

    QJsonObject diagnosticSummary;
    QMap<QString, QVariant> diagnosticMetrics;
    QString reportHtml;
};

class TestTaskContextManager : public QObject
{
    Q_OBJECT

public:
    explicit TestTaskContextManager(QObject *parent = nullptr);

    void setStorageDir(const QString &dirPath);
    QString storageDir() const;

    QString createTask(const QString &runId,
                       const QString &boardId,
                       const QString &componentRef,
                       const QString &pluginId);

    bool setParamSnapshot(const QString &taskId,
                          const QVector<TPSParamDefinition> &definitions,
                          const QMap<QString, QVariant> &values);

    bool setPortAllocation(const QString &taskId,
                           const QVector<TPSPortBinding> &bindings);

    bool setWiringGuide(const QString &taskId,
                        const QJsonArray &wiringGuide);

    bool setRoiBindings(const QString &taskId,
                        const QStringList &roiBindings);

    bool setDeviceConfig(const QString &taskId,
                         const JYDeviceConfig &config532x,
                         const JYDeviceConfig &config5711,
                         const JYDeviceConfig &config8902);

    bool setRawDataSummary(const QString &taskId,
                           const QMap<QString, DiagnosticSignalSeries> &signalSeries);

    bool setDiagnosticResult(const QString &taskId,
                             const DiagnosticReport &report,
                             const QString &reportHtml);

    bool closeTask(const QString &taskId,
                   const QString &status,
                   const QString &reason = QString());

    TaskContextRecord task(const QString &taskId) const;
    QVector<TaskContextRecord> tasks() const;

    bool persistTask(const QString &taskId, QString *errorMessage = nullptr) const;

private:
    QString m_storageDir;
    QMap<QString, TaskContextRecord> m_tasks;

    static QString paramTypeName(TPSParamType type);
    static QString deviceKindName(JYDeviceKind kind);
    static QJsonObject configToJson(const JYDeviceConfig &config);

    static QJsonObject toJson(const TaskPortHandle &handle);
    static QJsonObject toJson(const TaskContextRecord &record);

    QString resolveStorageDir() const;
    bool ensureStorageDir(QString *errorMessage = nullptr) const;
};

#endif // TESTTASKCONTEXTMANAGER_H
