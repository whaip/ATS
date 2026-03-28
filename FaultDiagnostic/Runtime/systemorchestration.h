#ifndef SYSTEMORCHESTRATION_H
#define SYSTEMORCHESTRATION_H

#include <QObject>
#include <QHash>
#include <QMap>
#include <QVector>
#include <QString>
#include <QVariant>
#include <QJsonObject>

#include "../../IODevices/JYDevices/jydevicetype.h"
#include "../../HDCamera/cameratypes.h"

class JYThreadManager;
class CameraStation;

struct SignalRequest {
    QString id;
    QString type;
    double value = 0.0;
    QString unit;
};

struct ResourceBinding {
    JYDeviceKind kind = JYDeviceKind::PXIe5322;
    int channel = 0;
    int slot = 0;
    QString resourceId;
};

struct ResourceMapping {
    QString signalType;
    ResourceBinding binding;
};

class ResourceManagementService : public QObject
{
    Q_OBJECT
public:
    explicit ResourceManagementService(QObject *parent = nullptr);

    void setMappings(const QVector<ResourceMapping> &mappings);
    bool resolveRequest(const SignalRequest &request, ResourceBinding *binding, QString *error) const;
    QVector<ResourceMapping> mappings() const;

private:
    QHash<QString, ResourceBinding> m_lookup;
};

class MaintenanceLogService : public QObject
{
    Q_OBJECT
public:
    explicit MaintenanceLogService(QObject *parent = nullptr);

    bool openSession(const QString &runId, const QString &boardId, QString *error);
    void closeSession();

    void recordEvent(const QString &type, const QJsonObject &payload = QJsonObject());
    void recordBatch(const JYAlignedBatch &batch);
    void recordImage(const ImageData &image, const QString &tag);

    QString sessionPath() const;
    QString logFilePath() const;

private:
    QString ensureSessionDir(const QString &runId, const QString &boardId);
    void appendJsonLine(const QJsonObject &obj);

    QString m_sessionDir;
    QString m_logPath;
};

enum class RuntimeState {
    Idle,
    Running,
    Paused,
    Aborted,
    Completed,
    Faulted
};

enum class RuntimeAction {
    Start,
    Pause,
    Abort,
    Configure
};

class FrameworkSecurityService : public QObject
{
    Q_OBJECT
public:
    struct UserContext {
        QString userId;
        QString role;
    };

    explicit FrameworkSecurityService(QObject *parent = nullptr);

    void setUser(const UserContext &user);
    UserContext user() const;

    bool authorize(RuntimeAction action) const;

private:
    UserContext m_user;
};

class RuntimeServices : public QObject
{
    Q_OBJECT
public:
    explicit RuntimeServices(QObject *parent = nullptr);

    void setThreadManager(JYThreadManager *manager);
    void setCameraStation(CameraStation *station);
    void setResourceManager(ResourceManagementService *rms);
    void setLogService(MaintenanceLogService *mtd);
    void setSecurityService(FrameworkSecurityService *frm);

    RuntimeState state() const;

    bool startRun(const QString &runId,
                  const QString &boardId,
                  const QVector<SignalRequest> &requests,
                  const JYDeviceConfig &config532x,
                  const JYDeviceConfig &config5711,
                  const JYDeviceConfig &config8902,
                  int timeoutMs,
                  QString *error);
    bool pauseRun(int timeoutMs, QString *error);
    bool completeRun(int timeoutMs, QString *error);
    bool resumeRun(int timeoutMs, QString *error);
    bool abortRun(QString *error);

    bool captureSnapshot(const QString &tag, QString *error);

signals:
    void stateChanged(RuntimeState state);
    void batchReady(const JYAlignedBatch &batch);
    void snapshotReady(const ImageData &image);
    void runtimeError(const QString &message);

private slots:
    void onAlignedBatch(const JYAlignedBatch &batch);

private:
    void setState(RuntimeState state);
    bool ensureAuthorized(RuntimeAction action, QString *error) const;
    bool ensureRuntimeReady(QString *error) const;
    void handleDeviceFault(JYDeviceKind kind, const QString &message);

    JYThreadManager *m_manager = nullptr;
    CameraStation *m_camera = nullptr;
    ResourceManagementService *m_rms = nullptr;
    MaintenanceLogService *m_mtd = nullptr;
    FrameworkSecurityService *m_frm = nullptr;

    RuntimeState m_state = RuntimeState::Idle;

    QVector<SignalRequest> m_lastRequests;
    QString m_lastRunId;
    QString m_lastBoardId;
    JYDeviceConfig m_lastConfig532x;
    JYDeviceConfig m_lastConfig5711;
    JYDeviceConfig m_lastConfig8902;

    QMetaObject::Connection m_batchConn;
    QMetaObject::Connection m_deviceStatusConn;
};

class SystemRuntimeOrchestration : public QObject
{
    Q_OBJECT
public:
    explicit SystemRuntimeOrchestration(QObject *parent = nullptr);

    ResourceManagementService *rms() const;
    MaintenanceLogService *mtd() const;
    FrameworkSecurityService *frm() const;
    RuntimeServices *rts() const;

    void setThreadManager(JYThreadManager *manager);
    void setCameraStation(CameraStation *station);

private:
    ResourceManagementService *m_rms = nullptr;
    MaintenanceLogService *m_mtd = nullptr;
    FrameworkSecurityService *m_frm = nullptr;
    RuntimeServices *m_rts = nullptr;
};

#endif // SYSTEMORCHESTRATION_H
