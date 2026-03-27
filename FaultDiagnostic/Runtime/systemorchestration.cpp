#include "systemorchestration.h"

#include "../../IODevices/JYDevices/jythreadmanager.h"
#include "../../IODevices/JYDevices/jydeviceorchestrator.h"
#include "../../IODevices/JYDevices/jydatapipeline.h"
#include "../../HDCamera/camerastation.h"
#include "../../logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {
QString keyForSignalType(const QString &type)
{
    return type.trimmed().toLower();
}
}

ResourceManagementService::ResourceManagementService(QObject *parent)
    : QObject(parent)
{
}

void ResourceManagementService::setMappings(const QVector<ResourceMapping> &mappings)
{
    m_lookup.clear();
    for (const auto &mapping : mappings) {
        const QString key = keyForSignalType(mapping.signalType);
        if (!key.isEmpty()) {
            m_lookup.insert(key, mapping.binding);
        }
    }
}

bool ResourceManagementService::resolveRequest(const SignalRequest &request, ResourceBinding *binding, QString *error) const
{
    if (!binding) {
        return false;
    }
    const QString key = keyForSignalType(request.type);
    if (key.isEmpty()) {
        if (error) {
            *error = QStringLiteral("empty signal type");
        }
        return false;
    }
    if (!m_lookup.contains(key)) {
        if (error) {
            *error = QStringLiteral("no resource mapping for %1").arg(request.type);
        }
        return false;
    }
    *binding = m_lookup.value(key);
    return true;
}

QVector<ResourceMapping> ResourceManagementService::mappings() const
{
    QVector<ResourceMapping> items;
    items.reserve(m_lookup.size());
    for (auto it = m_lookup.cbegin(); it != m_lookup.cend(); ++it) {
        ResourceMapping mapping;
        mapping.signalType = it.key();
        mapping.binding = it.value();
        items.push_back(mapping);
    }
    return items;
}

MaintenanceLogService::MaintenanceLogService(QObject *parent)
    : QObject(parent)
{
}

bool MaintenanceLogService::openSession(const QString &runId, const QString &boardId, QString *error)
{
    m_sessionDir = ensureSessionDir(runId, boardId);
    if (m_sessionDir.isEmpty()) {
        if (error) {
            *error = QStringLiteral("failed to create log directory");
        }
        return false;
    }

    m_logPath = QDir(m_sessionDir).filePath(QStringLiteral("runtime.jsonl"));
    QFile file(m_logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("failed to open log file");
        }
        return false;
    }
    file.close();

    recordEvent(QStringLiteral("session_start"), QJsonObject{{QStringLiteral("boardId"), boardId}});
    return true;
}

void MaintenanceLogService::closeSession()
{
    if (!m_logPath.isEmpty()) {
        recordEvent(QStringLiteral("session_end"));
    }
    m_sessionDir.clear();
    m_logPath.clear();
}

void MaintenanceLogService::recordEvent(const QString &type, const QJsonObject &payload)
{
    QJsonObject obj = payload;
    obj.insert(QStringLiteral("type"), type);
    obj.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODate));
    appendJsonLine(obj);
    Logger::log(QStringLiteral("MTD: %1").arg(type));
}

void MaintenanceLogService::recordBatch(const JYAlignedBatch &batch)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("aligned_batch"));
    obj.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODate));
    obj.insert(QStringLiteral("batchTimestamp"), batch.timestampMs);

    QJsonArray packets;
    for (auto it = batch.packets.begin(); it != batch.packets.end(); ++it) {
        const JYDataPacket &packet = it.value();
        QJsonObject packetObj;
        packetObj.insert(QStringLiteral("kind"), static_cast<int>(packet.kind));
        packetObj.insert(QStringLiteral("channelCount"), packet.channelCount);
        packetObj.insert(QStringLiteral("dataSize"), packet.data.size());
        packetObj.insert(QStringLiteral("timestampMs"), packet.timestampMs);
        packets.append(packetObj);
    }
    obj.insert(QStringLiteral("packets"), packets);
    appendJsonLine(obj);
}

void MaintenanceLogService::recordImage(const ImageData &image, const QString &tag)
{
    if (m_sessionDir.isEmpty() || image.image.isNull()) {
        return;
    }

    const QString imageName = QStringLiteral("image_%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"));
    const QString imagePath = QDir(m_sessionDir).filePath(imageName);
    image.image.save(imagePath);

    QJsonObject obj;
    obj.insert(QStringLiteral("type"), QStringLiteral("snapshot"));
    obj.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODate));
    obj.insert(QStringLiteral("tag"), tag);
    obj.insert(QStringLiteral("path"), imagePath);
    obj.insert(QStringLiteral("imageTimestampMs"), image.timestampMs);
    appendJsonLine(obj);
}

QString MaintenanceLogService::sessionPath() const
{
    return m_sessionDir;
}

QString MaintenanceLogService::logFilePath() const
{
    return m_logPath;
}

QString MaintenanceLogService::ensureSessionDir(const QString &runId, const QString &boardId)
{
    const QString baseDir = QCoreApplication::applicationDirPath();
    QDir dir(baseDir);
    if (!dir.exists(QStringLiteral("runtime_logs"))) {
        dir.mkpath(QStringLiteral("runtime_logs"));
    }
    dir.cd(QStringLiteral("runtime_logs"));

    const QString sanitizedRunId = runId.isEmpty() ? QStringLiteral("run") : runId;
    const QString folderName = QStringLiteral("%1_%2")
                                   .arg(boardId.isEmpty() ? QStringLiteral("board") : boardId)
                                   .arg(sanitizedRunId);
    if (!dir.exists(folderName)) {
        dir.mkpath(folderName);
    }
    dir.cd(folderName);
    return dir.absolutePath();
}

void MaintenanceLogService::appendJsonLine(const QJsonObject &obj)
{
    if (m_logPath.isEmpty()) {
        return;
    }

    QFile file(m_logPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return;
    }
    const QJsonDocument doc(obj);
    file.write(doc.toJson(QJsonDocument::Compact));
    file.write("\n");
}

FrameworkSecurityService::FrameworkSecurityService(QObject *parent)
    : QObject(parent)
{
}

void FrameworkSecurityService::setUser(const UserContext &user)
{
    m_user = user;
}

FrameworkSecurityService::UserContext FrameworkSecurityService::user() const
{
    return m_user;
}

bool FrameworkSecurityService::authorize(RuntimeAction action) const
{
    const QString role = m_user.role.trimmed().toLower();
    if (role.isEmpty()) {
        return false;
    }
    if (role == QStringLiteral("admin")) {
        return true;
    }
    if (role == QStringLiteral("operator")) {
        return action == RuntimeAction::Start || action == RuntimeAction::Pause;
    }
    return false;
}

RuntimeServices::RuntimeServices(QObject *parent)
    : QObject(parent)
{
}

void RuntimeServices::setThreadManager(JYThreadManager *manager)
{
    if (m_manager == manager) {
        return;
    }
    if (m_batchConn) {
        disconnect(m_batchConn);
    }
    if (m_deviceStatusConn) {
        disconnect(m_deviceStatusConn);
    }
    m_manager = manager;
    if (m_manager && m_manager->pipeline()) {
        m_batchConn = connect(m_manager->pipeline(), &JYDataPipeline::alignedBatchReady, this, &RuntimeServices::onAlignedBatch);
    }
    if (m_manager) {
        m_deviceStatusConn = connect(m_manager,
                                     &JYThreadManager::deviceStatusChanged,
                                     this,
                                     [this](JYDeviceKind kind, JYDeviceState state, const QString &message) {
                                         if (state == JYDeviceState::Faulted) {
                                             handleDeviceFault(kind, message);
                                         }
                                     });
    }
}

void RuntimeServices::setCameraStation(CameraStation *station)
{
    m_camera = station;
}

void RuntimeServices::setResourceManager(ResourceManagementService *rms)
{
    m_rms = rms;
}

void RuntimeServices::setLogService(MaintenanceLogService *mtd)
{
    m_mtd = mtd;
}

void RuntimeServices::setSecurityService(FrameworkSecurityService *frm)
{
    m_frm = frm;
}

RuntimeState RuntimeServices::state() const
{
    return m_state;
}

bool RuntimeServices::startRun(const QString &runId,
                               const QString &boardId,
                               const QVector<SignalRequest> &requests,
                               const JYDeviceConfig &config532x,
                               const JYDeviceConfig &config5711,
                               const JYDeviceConfig &config8902,
                               int timeoutMs,
                               QString *error)
{
    if (!ensureAuthorized(RuntimeAction::Start, error)) {
        emit runtimeError(error ? *error : QString());
        return false;
    }
    if (!ensureRuntimeReady(error)) {
        emit runtimeError(error ? *error : QString());
        return false;
    }
    if (m_state == RuntimeState::Running) {
        if (error) {
            *error = QStringLiteral("runtime already running");
        }
        return false;
    }

    QVector<ResourceBinding> bindings;
    for (const auto &request : requests) {
        ResourceBinding binding;
        QString reason;
        if (!m_rms->resolveRequest(request, &binding, &reason)) {
            if (error) {
                *error = reason;
            }
            emit runtimeError(reason);
            return false;
        }
        bindings.push_back(binding);
    }

    QString logError;
    if (m_mtd && !m_mtd->openSession(runId, boardId, &logError)) {
        if (error) {
            *error = logError;
        }
        emit runtimeError(logError);
        return false;
    }

    if (m_mtd) {
        QJsonArray mapping;
        for (const auto &binding : bindings) {
            QJsonObject entry;
            entry.insert(QStringLiteral("kind"), static_cast<int>(binding.kind));
            entry.insert(QStringLiteral("channel"), binding.channel);
            entry.insert(QStringLiteral("slot"), binding.slot);
            entry.insert(QStringLiteral("resourceId"), binding.resourceId);
            mapping.append(entry);
        }
        m_mtd->recordEvent(QStringLiteral("resource_mapped"), QJsonObject{{QStringLiteral("bindings"), mapping}});
        m_mtd->recordEvent(QStringLiteral("configure_barrier_enter"));
    }

    m_lastRunId = runId;
    m_lastBoardId = boardId;
    m_lastRequests = requests;
    m_lastConfig532x = config532x;
    m_lastConfig5711 = config5711;
    m_lastConfig8902 = config8902;

    auto *orchestrator = m_manager->orchestrator();
    qint64 barrierReleaseMs = 0;
    const bool ok = orchestrator->synchronizeStart(config532x,
                                                   config5711,
                                                   config8902,
                                                   timeoutMs,
                                                   &barrierReleaseMs);
    if (!ok) {
        if (error) {
            *error = QStringLiteral("device orchestration failed");
        }
        setState(RuntimeState::Faulted);
        emit runtimeError(error ? *error : QString());
        return false;
    }

    if (m_manager && m_manager->pipeline()) {
        m_manager->pipeline()->setSyncAnchorMs(barrierReleaseMs);
    }

    if (m_mtd) {
        m_mtd->recordEvent(QStringLiteral("configure_barrier_released"));
        m_mtd->recordEvent(QStringLiteral("start_barrier_released"),
                           QJsonObject{{QStringLiteral("t0_ms"), static_cast<qint64>(barrierReleaseMs)}});
    }

    setState(RuntimeState::Running);
    return true;
}

bool RuntimeServices::pauseRun(int timeoutMs, QString *error)
{
    if (!ensureAuthorized(RuntimeAction::Pause, error)) {
        emit runtimeError(error ? *error : QString());
        return false;
    }
    if (!ensureRuntimeReady(error)) {
        emit runtimeError(error ? *error : QString());
        return false;
    }
    if (m_state != RuntimeState::Running) {
        if (error) {
            *error = QStringLiteral("runtime not running");
        }
        return false;
    }

    const bool ok = m_manager->orchestrator()->synchronizeStop(timeoutMs);
    if (!ok) {
        if (error) {
            *error = QStringLiteral("pause failed");
        }
        setState(RuntimeState::Faulted);
        emit runtimeError(error ? *error : QString());
        return false;
    }

    setState(RuntimeState::Paused);
    if (m_mtd) {
        m_mtd->recordEvent(QStringLiteral("paused"));
    }
    return true;
}

bool RuntimeServices::resumeRun(int timeoutMs, QString *error)
{
    if (!ensureAuthorized(RuntimeAction::Start, error)) {
        emit runtimeError(error ? *error : QString());
        return false;
    }
    if (!ensureRuntimeReady(error)) {
        emit runtimeError(error ? *error : QString());
        return false;
    }
    if (m_state != RuntimeState::Paused) {
        if (error) {
            *error = QStringLiteral("runtime not paused");
        }
        return false;
    }

    const bool ok = m_manager->orchestrator()->synchronizeStart(m_lastConfig532x, m_lastConfig5711, m_lastConfig8902, timeoutMs);
    if (!ok) {
        if (error) {
            *error = QStringLiteral("resume failed");
        }
        setState(RuntimeState::Faulted);
        emit runtimeError(error ? *error : QString());
        return false;
    }

    setState(RuntimeState::Running);
    if (m_mtd) {
        m_mtd->recordEvent(QStringLiteral("resumed"));
    }
    return true;
}

bool RuntimeServices::abortRun(QString *error)
{
    if (!ensureAuthorized(RuntimeAction::Abort, error)) {
        emit runtimeError(error ? *error : QString());
        return false;
    }
    if (!ensureRuntimeReady(error)) {
        emit runtimeError(error ? *error : QString());
        return false;
    }

    m_manager->orchestrator()->closeAll();
    setState(RuntimeState::Aborted);
    if (m_mtd) {
        m_mtd->recordEvent(QStringLiteral("aborted"));
        m_mtd->closeSession();
    }
    return true;
}

void RuntimeServices::handleDeviceFault(JYDeviceKind kind, const QString &message)
{
    if (m_state != RuntimeState::Running) {
        return;
    }

    const QString detail = QStringLiteral("device fault: kind=%1, message=%2")
                               .arg(static_cast<int>(kind))
                               .arg(message);

    if (m_mtd) {
        m_mtd->recordEvent(QStringLiteral("runtime_fault"),
                           QJsonObject{{QStringLiteral("kind"), static_cast<int>(kind)},
                                       {QStringLiteral("message"), message}});
    }

    if (m_manager && m_manager->orchestrator()) {
        m_manager->orchestrator()->closeAll();
    }

    setState(RuntimeState::Faulted);
    emit runtimeError(detail);
}

bool RuntimeServices::captureSnapshot(const QString &tag, QString *error)
{
    if (!m_camera) {
        if (error) {
            *error = QStringLiteral("camera station not set");
        }
        return false;
    }
    ImageData data;
    if (!m_camera->tryGetLatestImage(&data)) {
        if (error) {
            *error = QStringLiteral("no image available");
        }
        return false;
    }
    if (m_mtd) {
        m_mtd->recordImage(data, tag);
    }
    emit snapshotReady(data);
    return true;
}

void RuntimeServices::onAlignedBatch(const JYAlignedBatch &batch)
{
    if (m_state != RuntimeState::Running) {
        return;
    }
    if (m_mtd) {
        m_mtd->recordBatch(batch);
    }
    emit batchReady(batch);
}

void RuntimeServices::setState(RuntimeState state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(state);
}

bool RuntimeServices::ensureAuthorized(RuntimeAction action, QString *error) const
{
    if (!m_frm) {
        return true;
    }
    if (!m_frm->authorize(action)) {
        if (error) {
            *error = QStringLiteral("permission denied");
        }
        return false;
    }
    return true;
}

bool RuntimeServices::ensureRuntimeReady(QString *error) const
{
    if (!m_manager || !m_manager->orchestrator()) {
        if (error) {
            *error = QStringLiteral("device manager not ready");
        }
        return false;
    }
    if (!m_rms) {
        if (error) {
            *error = QStringLiteral("resource manager not set");
        }
        return false;
    }
    return true;
}

SystemRuntimeOrchestration::SystemRuntimeOrchestration(QObject *parent)
    : QObject(parent)
{
    m_rms = new ResourceManagementService(this);
    m_mtd = new MaintenanceLogService(this);
    m_frm = new FrameworkSecurityService(this);
    m_rts = new RuntimeServices(this);

    m_rts->setResourceManager(m_rms);
    m_rts->setLogService(m_mtd);
    m_rts->setSecurityService(m_frm);
}

ResourceManagementService *SystemRuntimeOrchestration::rms() const
{
    return m_rms;
}

MaintenanceLogService *SystemRuntimeOrchestration::mtd() const
{
    return m_mtd;
}

FrameworkSecurityService *SystemRuntimeOrchestration::frm() const
{
    return m_frm;
}

RuntimeServices *SystemRuntimeOrchestration::rts() const
{
    return m_rts;
}

void SystemRuntimeOrchestration::setThreadManager(JYThreadManager *manager)
{
    if (m_rts) {
        m_rts->setThreadManager(manager);
    }
}

void SystemRuntimeOrchestration::setCameraStation(CameraStation *station)
{
    if (m_rts) {
        m_rts->setCameraStation(station);
    }
}
