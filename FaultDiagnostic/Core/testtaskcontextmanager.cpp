#include "testtaskcontextmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace {
QJsonObject variantMapToJsonObject(const QMap<QString, QVariant> &values)
{
    QJsonObject object;
    for (auto it = values.begin(); it != values.end(); ++it) {
        object.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    return object;
}
}

TestTaskContextManager::TestTaskContextManager(QObject *parent)
    : QObject(parent)
{
}

void TestTaskContextManager::setStorageDir(const QString &dirPath)
{
    m_storageDir = QDir::cleanPath(dirPath);
}

QString TestTaskContextManager::storageDir() const
{
    return resolveStorageDir();
}

QString TestTaskContextManager::createTask(const QString &runId,
                                           const QString &boardId,
                                           const QString &componentRef,
                                           const QString &pluginId)
{
    TaskContextRecord record;
    QString baseId = componentRef.trimmed();
    if (baseId.isEmpty()) {
        baseId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    baseId.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    if (baseId.isEmpty()) {
        baseId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QString taskId = baseId;
    int suffix = 1;
    while (m_tasks.contains(taskId)) {
        ++suffix;
        taskId = QStringLiteral("%1_%2").arg(baseId).arg(suffix);
    }

    record.taskId = taskId;
    record.runId = runId;
    record.boardId = boardId;
    record.componentRef = componentRef;
    record.pluginId = pluginId;
    record.createdAt = QDateTime::currentDateTime();
    record.status = QStringLiteral("Created");

    m_tasks.insert(record.taskId, record);
    persistTask(record.taskId, nullptr);
    return record.taskId;
}

bool TestTaskContextManager::setParamSnapshot(const QString &taskId,
                                              const QVector<TPSParamDefinition> &definitions,
                                              const QMap<QString, QVariant> &values)
{
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    QJsonArray schema;
    for (const auto &def : definitions) {
        QJsonObject item;
        item.insert(QStringLiteral("key"), def.key);
        item.insert(QStringLiteral("label"), def.label);
        item.insert(QStringLiteral("type"), paramTypeName(def.type));
        item.insert(QStringLiteral("required"), def.required);
        item.insert(QStringLiteral("unit"), def.unit);
        item.insert(QStringLiteral("defaultValue"), QJsonValue::fromVariant(def.defaultValue));
        item.insert(QStringLiteral("minValue"), QJsonValue::fromVariant(def.minValue));
        item.insert(QStringLiteral("maxValue"), QJsonValue::fromVariant(def.maxValue));
        item.insert(QStringLiteral("stepValue"), QJsonValue::fromVariant(def.stepValue));

        QJsonArray options;
        for (const QString &option : def.enumOptions) {
            options.append(option);
        }
        item.insert(QStringLiteral("enumOptions"), options);
        schema.append(item);
    }

    it->parameterSchema = schema;
    it->parameterSnapshot = values;
    it->status = QStringLiteral("ParamSnapshotted");
    return persistTask(taskId, nullptr);
}

bool TestTaskContextManager::setPortAllocation(const QString &taskId,
                                               const QVector<TPSPortBinding> &bindings)
{
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    QVector<TaskPortHandle> handles;
    handles.reserve(bindings.size());
    for (const auto &binding : bindings) {
        TaskPortHandle handle;
        handle.taskId = taskId;
        handle.identifier = binding.identifier;
        handle.deviceKind = deviceKindName(binding.deviceKind);
        handle.type = QString::number(static_cast<int>(binding.type));
        handle.channel = binding.channel;
        handle.resourceId = binding.resourceId;
        handle.occupied = true;
        handles.push_back(handle);
    }

    it->portHandles = handles;
    it->status = QStringLiteral("PortsAllocated");
    return persistTask(taskId, nullptr);
}

bool TestTaskContextManager::setWiringGuide(const QString &taskId,
                                            const QJsonArray &wiringGuide)
{
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    it->wiringGuide = wiringGuide;
    it->status = QStringLiteral("WiringGenerated");
    return persistTask(taskId, nullptr);
}

bool TestTaskContextManager::setRoiBindings(const QString &taskId,
                                            const QStringList &roiBindings)
{
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    it->roiBindings = roiBindings;
    if (!roiBindings.isEmpty()) {
        it->status = QStringLiteral("RoiBound");
    }
    return persistTask(taskId, nullptr);
}

bool TestTaskContextManager::setDeviceConfig(const QString &taskId,
                                             const JYDeviceConfig &config532x,
                                             const JYDeviceConfig &config5711,
                                             const JYDeviceConfig &config8902)
{
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    QJsonObject object;
    object.insert(QStringLiteral("cfg532x"), configToJson(config532x));
    object.insert(QStringLiteral("cfg5711"), configToJson(config5711));
    object.insert(QStringLiteral("cfg8902"), configToJson(config8902));

    it->deviceConfig = object;
    it->status = QStringLiteral("DeviceConfigured");
    return persistTask(taskId, nullptr);
}

bool TestTaskContextManager::setRawDataSummary(const QString &taskId,
                                               const QMap<QString, DiagnosticSignalSeries> &signalSeries)
{
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    QJsonArray channels;
    int totalSamples = 0;
    for (auto seriesIt = signalSeries.begin(); seriesIt != signalSeries.end(); ++seriesIt) {
        QJsonObject channel;
        channel.insert(QStringLiteral("signal"), seriesIt.key());
        channel.insert(QStringLiteral("sampleCount"), seriesIt.value().samples.size());
        channel.insert(QStringLiteral("sampleRateHz"), seriesIt.value().sampleRateHz);
        channels.append(channel);
        totalSamples += seriesIt.value().samples.size();
    }

    QJsonObject summary;
    summary.insert(QStringLiteral("channelCount"), signalSeries.size());
    summary.insert(QStringLiteral("totalSamples"), totalSamples);
    summary.insert(QStringLiteral("channels"), channels);
    summary.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));

    it->rawDataSummary = summary;
    it->status = QStringLiteral("RawCaptured");
    return persistTask(taskId, nullptr);
}

bool TestTaskContextManager::setDiagnosticResult(const QString &taskId,
                                                 const DiagnosticReport &report,
                                                 const QString &reportHtml)
{
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    QJsonObject summary;
    summary.insert(QStringLiteral("componentRef"), report.componentRef);
    summary.insert(QStringLiteral("componentType"), report.componentType);
    summary.insert(QStringLiteral("success"), report.success);
    summary.insert(QStringLiteral("summary"), report.summary);
    summary.insert(QStringLiteral("detailHtml"), report.detailHtml);

    it->diagnosticSummary = summary;
    it->diagnosticMetrics = report.metrics;
    it->reportHtml = reportHtml;
    it->status = QStringLiteral("Diagnosed");
    return persistTask(taskId, nullptr);
}

bool TestTaskContextManager::closeTask(const QString &taskId,
                                       const QString &status,
                                       const QString &reason)
{
    auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        return false;
    }

    it->closedAt = QDateTime::currentDateTime();
    it->status = status.trimmed().isEmpty() ? QStringLiteral("Closed") : status;
    it->closeReason = reason.trimmed();
    return persistTask(taskId, nullptr);
}

TaskContextRecord TestTaskContextManager::task(const QString &taskId) const
{
    return m_tasks.value(taskId);
}

QVector<TaskContextRecord> TestTaskContextManager::tasks() const
{
    QVector<TaskContextRecord> list;
    list.reserve(m_tasks.size());
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        list.push_back(it.value());
    }
    return list;
}

bool TestTaskContextManager::persistTask(const QString &taskId, QString *errorMessage) const
{
    const auto it = m_tasks.find(taskId);
    if (it == m_tasks.end()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("task not found: %1").arg(taskId);
        }
        return false;
    }

    if (!ensureStorageDir(errorMessage)) {
        return false;
    }

    const QString filePath = QDir(resolveStorageDir()).filePath(QStringLiteral("%1.json").arg(taskId));
    QSaveFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("cannot open task file: %1").arg(filePath);
        }
        return false;
    }

    const QJsonDocument doc(toJson(it.value()));
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    if (saveFile.write(bytes) != bytes.size() || !saveFile.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("failed to write task file: %1").arg(filePath);
        }
        return false;
    }

    return true;
}

QString TestTaskContextManager::paramTypeName(TPSParamType type)
{
    switch (type) {
    case TPSParamType::String:
        return QStringLiteral("String");
    case TPSParamType::Integer:
        return QStringLiteral("Integer");
    case TPSParamType::Double:
        return QStringLiteral("Double");
    case TPSParamType::Boolean:
        return QStringLiteral("Boolean");
    case TPSParamType::Enum:
        return QStringLiteral("Enum");
    }
    return QStringLiteral("Unknown");
}

QString TestTaskContextManager::deviceKindName(JYDeviceKind kind)
{
    switch (kind) {
    case JYDeviceKind::PXIe5322:
        return QStringLiteral("PXIe5322");
    case JYDeviceKind::PXIe5323:
        return QStringLiteral("PXIe5323");
    case JYDeviceKind::PXIe5711:
        return QStringLiteral("PXIe5711");
    case JYDeviceKind::PXIe8902:
        return QStringLiteral("PXIe8902");
    }
    return QStringLiteral("Unknown");
}

QJsonObject TestTaskContextManager::configToJson(const JYDeviceConfig &config)
{
    QJsonObject object;
    object.insert(QStringLiteral("kind"), deviceKindName(config.kind));

    QJsonObject c532x;
    c532x.insert(QStringLiteral("slotNumber"), config.cfg532x.slotNumber);
    c532x.insert(QStringLiteral("channelCount"), config.cfg532x.channelCount);
    c532x.insert(QStringLiteral("sampleRate"), config.cfg532x.sampleRate);
    c532x.insert(QStringLiteral("samplesPerRead"), config.cfg532x.samplesPerRead);
    c532x.insert(QStringLiteral("timeoutMs"), config.cfg532x.timeoutMs);
    c532x.insert(QStringLiteral("lowRange"), config.cfg532x.lowRange);
    c532x.insert(QStringLiteral("highRange"), config.cfg532x.highRange);
    c532x.insert(QStringLiteral("bandwidth"), config.cfg532x.bandwidth);
    object.insert(QStringLiteral("cfg532x"), c532x);

    QJsonObject c5711;
    c5711.insert(QStringLiteral("slotNumber"), config.cfg5711.slotNumber);
    c5711.insert(QStringLiteral("channelCount"), config.cfg5711.channelCount);
    c5711.insert(QStringLiteral("sampleRate"), config.cfg5711.sampleRate);
    c5711.insert(QStringLiteral("lowRange"), config.cfg5711.lowRange);
    c5711.insert(QStringLiteral("highRange"), config.cfg5711.highRange);

    QJsonArray enabledChannels;
    for (int channel : config.cfg5711.enabledChannels) {
        enabledChannels.append(channel);
    }
    c5711.insert(QStringLiteral("enabledChannels"), enabledChannels);

    QJsonArray waveforms;
    for (const auto &waveform : config.cfg5711.waveforms) {
        QJsonObject item;
        item.insert(QStringLiteral("channel"), waveform.channel);
        item.insert(QStringLiteral("waveformType"), static_cast<int>(waveform.type));
        item.insert(QStringLiteral("amplitude"), waveform.amplitude);
        item.insert(QStringLiteral("frequency"), waveform.frequency);
        item.insert(QStringLiteral("dutyCycle"), waveform.dutyCycle);
        item.insert(QStringLiteral("pulseVLow"), waveform.pulseVLow);
        item.insert(QStringLiteral("pulseVHigh"), waveform.pulseVHigh);
        item.insert(QStringLiteral("pulseTDelay"), waveform.pulseTDelay);
        item.insert(QStringLiteral("pulseTOn"), waveform.pulseTOn);
        item.insert(QStringLiteral("pulseTPeriod"), waveform.pulseTPeriod);
        item.insert(QStringLiteral("pulseUseTiming"), waveform.pulseUseTiming);
        waveforms.append(item);
    }
    c5711.insert(QStringLiteral("waveforms"), waveforms);
    object.insert(QStringLiteral("cfg5711"), c5711);

    QJsonObject c8902;
    c8902.insert(QStringLiteral("slotNumber"), config.cfg8902.slotNumber);
    c8902.insert(QStringLiteral("sampleCount"), config.cfg8902.sampleCount);
    c8902.insert(QStringLiteral("timeoutMs"), config.cfg8902.timeoutMs);
    c8902.insert(QStringLiteral("apertureTime"), config.cfg8902.apertureTime);
    c8902.insert(QStringLiteral("triggerDelay"), config.cfg8902.triggerDelay);
    c8902.insert(QStringLiteral("measurementFunction"), static_cast<int>(config.cfg8902.measurementFunction));
    c8902.insert(QStringLiteral("range"), static_cast<int>(config.cfg8902.range));
    object.insert(QStringLiteral("cfg8902"), c8902);

    return object;
}

QJsonObject TestTaskContextManager::toJson(const TaskPortHandle &handle)
{
    QJsonObject object;
    object.insert(QStringLiteral("taskId"), handle.taskId);
    object.insert(QStringLiteral("identifier"), handle.identifier);
    object.insert(QStringLiteral("type"), handle.type);
    object.insert(QStringLiteral("deviceKind"), handle.deviceKind);
    object.insert(QStringLiteral("channel"), handle.channel);
    object.insert(QStringLiteral("slot"), handle.slot);
    object.insert(QStringLiteral("resourceId"), handle.resourceId);
    object.insert(QStringLiteral("occupied"), handle.occupied);
    return object;
}

QJsonObject TestTaskContextManager::toJson(const TaskContextRecord &record)
{
    QJsonObject object;
    object.insert(QStringLiteral("taskId"), record.taskId);
    object.insert(QStringLiteral("runId"), record.runId);
    object.insert(QStringLiteral("boardId"), record.boardId);
    object.insert(QStringLiteral("componentRef"), record.componentRef);
    object.insert(QStringLiteral("pluginId"), record.pluginId);
    object.insert(QStringLiteral("createdAt"), record.createdAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("closedAt"), record.closedAt.isValid() ? record.closedAt.toString(Qt::ISODate) : QString());
    object.insert(QStringLiteral("status"), record.status);
    object.insert(QStringLiteral("closeReason"), record.closeReason);

    object.insert(QStringLiteral("parameterSchema"), record.parameterSchema);
    object.insert(QStringLiteral("parameterSnapshot"), variantMapToJsonObject(record.parameterSnapshot));

    QJsonArray portHandles;
    for (const auto &handle : record.portHandles) {
        portHandles.append(toJson(handle));
    }
    object.insert(QStringLiteral("portHandles"), portHandles);

    object.insert(QStringLiteral("wiringGuide"), record.wiringGuide);

    QJsonArray roiArray;
    for (const QString &roi : record.roiBindings) {
        roiArray.append(roi);
    }
    object.insert(QStringLiteral("roiBindings"), roiArray);

    object.insert(QStringLiteral("deviceConfig"), record.deviceConfig);
    object.insert(QStringLiteral("rawDataSummary"), record.rawDataSummary);
    object.insert(QStringLiteral("diagnosticSummary"), record.diagnosticSummary);
    object.insert(QStringLiteral("diagnosticMetrics"), variantMapToJsonObject(record.diagnosticMetrics));
    object.insert(QStringLiteral("reportHtml"), record.reportHtml);

    return object;
}

QString TestTaskContextManager::resolveStorageDir() const
{
    if (!m_storageDir.trimmed().isEmpty()) {
        return m_storageDir;
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("runtime_tasks"));
}

bool TestTaskContextManager::ensureStorageDir(QString *errorMessage) const
{
    QDir dir(resolveStorageDir());
    if (dir.exists()) {
        return true;
    }

    if (dir.mkpath(QStringLiteral("."))) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("cannot create directory: %1").arg(resolveStorageDir());
    }
    return false;
}
