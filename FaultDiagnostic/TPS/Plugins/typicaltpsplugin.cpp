#include "typicaltpsplugin.h"

#include "../Core/tpsruntimecontext.h"

#include "../../../IODevices/JYDevices/5711waveformconfig.h"
#include "../../../IODevices/JYDevices/jydeviceconfigutils.h"

namespace {
QString deviceKindName(JYDeviceKind kind)
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
}

TypicalTpsPlugin::TypicalTpsPlugin(QObject *parent)
    : QObject(parent)
{
}

QString TypicalTpsPlugin::pluginId() const
{
    return QStringLiteral("tps.typical");
}

QString TypicalTpsPlugin::displayName() const
{
    return QStringLiteral("Typical 温度检测 TPS");
}

QString TypicalTpsPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVector<TPSParamDefinition> TypicalTpsPlugin::parameterDefinitions() const
{
    return requirements().parameters;
}

TPSPluginRequirement TypicalTpsPlugin::requirements() const
{
    TPSPluginRequirement requirement;

    TPSParamDefinition outputPositiveAnchor;
    outputPositiveAnchor.key = QStringLiteral("outputPositiveAnchor");
    outputPositiveAnchor.label = QStringLiteral("正10V输出锚点");
    outputPositiveAnchor.type = TPSParamType::Integer;
    outputPositiveAnchor.defaultValue = -1;

    TPSParamDefinition outputNegativeAnchor;
    outputNegativeAnchor.key = QStringLiteral("outputNegativeAnchor");
    outputNegativeAnchor.label = QStringLiteral("-10V输出锚点");
    outputNegativeAnchor.type = TPSParamType::Integer;
    outputNegativeAnchor.defaultValue = -1;

    TPSParamDefinition temperatureAnchor;
    temperatureAnchor.key = QStringLiteral("temperatureAnchor");
    temperatureAnchor.label = QStringLiteral("测温区域锚点");
    temperatureAnchor.type = TPSParamType::Integer;
    temperatureAnchor.defaultValue = -1;

    TPSParamDefinition captureMs;
    captureMs.key = QStringLiteral("captureDurationMs");
    captureMs.label = QStringLiteral("测试时长(ms)");
    captureMs.type = TPSParamType::Integer;
    captureMs.defaultValue = 60000;
    captureMs.minValue = 1;
    captureMs.maxValue = 60000;
    captureMs.stepValue = 10;

    TPSParamDefinition temperatureWarn;
    temperatureWarn.key = QStringLiteral("temperatureWarnC");
    temperatureWarn.label = QStringLiteral("温度预警(℃)");
    temperatureWarn.type = TPSParamType::Double;
    temperatureWarn.defaultValue = 25.0;
    temperatureWarn.minValue = -40.0;
    temperatureWarn.maxValue = 250.0;
    temperatureWarn.stepValue = 0.1;

    TPSParamDefinition temperatureTrip;
    temperatureTrip.key = QStringLiteral("temperatureTripC");
    temperatureTrip.label = QStringLiteral("温度停机(℃)");
    temperatureTrip.type = TPSParamType::Double;
    temperatureTrip.defaultValue = 27.0;
    temperatureTrip.minValue = -40.0;
    temperatureTrip.maxValue = 250.0;
    temperatureTrip.stepValue = 0.1;

    TPSPortRequest positiveOut;
    positiveOut.type = TPSPortType::VoltageOutput;
    positiveOut.count = 1;
    positiveOut.identifiers = {QStringLiteral("typicalPositiveOutput")};

    TPSPortRequest negativeOut;
    negativeOut.type = TPSPortType::VoltageOutput;
    negativeOut.count = 1;
    negativeOut.identifiers = {QStringLiteral("typicalNegativeOutput")};

    requirement.parameters = {
        outputPositiveAnchor,
        outputNegativeAnchor,
        temperatureAnchor,
        captureMs,
        temperatureWarn,
        temperatureTrip
    };
    requirement.ports = {positiveOut, negativeOut};

    return requirement;
}

bool TypicalTpsPlugin::buildDevicePlan(const QVector<TPSPortBinding> &bindings,
                                       const QMap<QString, QVariant> &settings,
                                       TPSDevicePlan *plan,
                                       QString *error)
{
    if (!plan) {
        if (error) {
            *error = QStringLiteral("plan is null");
        }
        return false;
    }

    const QVector<TPSPortBinding> effectiveBindings = bindings.isEmpty() ? m_allocatedBindings : bindings;

    const TPSPortBinding *positiveOut = findBinding(effectiveBindings, QStringLiteral("typicalPositiveOutput"));
    const TPSPortBinding *negativeOut = findBinding(effectiveBindings, QStringLiteral("typicalNegativeOutput"));
    if (!positiveOut || !negativeOut) {
        if (error) {
            *error = QStringLiteral("missing required bindings for typical TPS");
        }
        return false;
    }

    if (positiveOut->deviceKind != JYDeviceKind::PXIe5711
        || negativeOut->deviceKind != JYDeviceKind::PXIe5711) {
        if (error) {
            *error = QStringLiteral("typical outputs must bind to PXIe5711");
        }
        return false;
    }

    TPSDevicePlan devicePlan;
    devicePlan.bindings = effectiveBindings;
    devicePlan.cfg532x = build532xInitConfig(JYDeviceKind::PXIe5322);
    devicePlan.cfg532x.cfg532x.channelCount = 0;
    devicePlan.cfg532x.cfg532x.slotNumber = -1;
    devicePlan.cfg5711 = build5711InitConfig();
    devicePlan.cfg8902 = build8902InitConfig();
    devicePlan.cfg8902.cfg8902.sampleCount = 0;
    devicePlan.cfg8902.cfg8902.slotNumber = -1;

    if (m_configReady) {
        devicePlan.cfg5711 = m_config5711;
    } else {
        const int maxOutChannel = qMax(positiveOut->channel, negativeOut->channel);
        devicePlan.cfg5711.cfg5711.channelCount = maxOutChannel + 1;
        devicePlan.cfg5711.cfg5711.enabledChannels = {positiveOut->channel, negativeOut->channel};
        devicePlan.cfg5711.cfg5711.waveforms.clear();

        devicePlan.cfg5711.cfg5711.waveforms.push_back(
            build5711WaveformConfig(positiveOut->channel,
                                    QStringLiteral("HighLevelWave"),
                                    PXIe5711_make_params({{"amplitude", 10.0}})));

        devicePlan.cfg5711.cfg5711.waveforms.push_back(
            build5711WaveformConfig(negativeOut->channel,
                                    QStringLiteral("HighLevelWave"),
                                    PXIe5711_make_params({{"amplitude", -10.0}})));
    }

    TPSSignalRequest positiveReq;
    positiveReq.id = positiveOut->identifier;
    positiveReq.signalType = positiveOut->identifier;
    positiveReq.value = 10.0;
    positiveReq.unit = QStringLiteral("V");
    devicePlan.requests.push_back(positiveReq);

    TPSSignalRequest negativeReq;
    negativeReq.id = negativeOut->identifier;
    negativeReq.signalType = negativeOut->identifier;
    negativeReq.value = -10.0;
    negativeReq.unit = QStringLiteral("V");
    devicePlan.requests.push_back(negativeReq);

    devicePlan.wiringSteps = {
        QStringLiteral("将 %1 连接到锚点 %2")
            .arg(portText(*positiveOut), anchorText(settings, QStringLiteral("outputPositiveAnchor"))),
        QStringLiteral("将 %1 连接到锚点 %2")
            .arg(portText(*negativeOut), anchorText(settings, QStringLiteral("outputNegativeAnchor")))
    };

    devicePlan.temperatureGuide = QStringLiteral("请在锚点 %1 附近框选测温区域")
        .arg(anchorText(settings, QStringLiteral("temperatureAnchor")));
    devicePlan.guide.wiringSteps = devicePlan.wiringSteps;
    devicePlan.guide.roiSteps = {devicePlan.temperatureGuide};
    devicePlan.guide.extensions.insert(QStringLiteral("pluginId"), pluginId());
    devicePlan.guide.extensions.insert(QStringLiteral("wiringFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("outputPositiveAnchor")),
        anchorText(settings, QStringLiteral("outputNegativeAnchor"))
    });
    devicePlan.guide.extensions.insert(QStringLiteral("roiFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("temperatureAnchor"))
    });
    devicePlan.guide.extensions.insert(QStringLiteral("default.temperatureWarnC"),
                                       settings.value(QStringLiteral("temperatureWarnC"), 60.0).toDouble());
    devicePlan.guide.extensions.insert(QStringLiteral("default.temperatureTripC"),
                                       settings.value(QStringLiteral("temperatureTripC"), 70.0).toDouble());

    *plan = devicePlan;
    return true;
}

bool TypicalTpsPlugin::configure(const QMap<QString, QVariant> &settings, QString *error)
{
    m_settings = settings;
    m_allocatedBindings = TPSRuntimeContext::decodeBindings(settings.value(TPSRuntimeContext::allocatedBindingsKey()));

    const TPSPortBinding *positiveOut = findBinding(m_allocatedBindings, QStringLiteral("typicalPositiveOutput"));
    const TPSPortBinding *negativeOut = findBinding(m_allocatedBindings, QStringLiteral("typicalNegativeOutput"));
    if (!positiveOut || !negativeOut) {
        m_configReady = false;
        if (error) {
            *error = QStringLiteral("missing allocated binding: typicalPositiveOutput/typicalNegativeOutput");
        }
        return false;
    }

    if (positiveOut->deviceKind != JYDeviceKind::PXIe5711
        || negativeOut->deviceKind != JYDeviceKind::PXIe5711) {
        m_configReady = false;
        if (error) {
            *error = QStringLiteral("typical outputs must bind to PXIe5711");
        }
        return false;
    }

    m_config5711 = build5711InitConfig();
    const int maxOutChannel = qMax(positiveOut->channel, negativeOut->channel);
    m_config5711.cfg5711.channelCount = maxOutChannel + 1;
    m_config5711.cfg5711.enabledChannels = {positiveOut->channel, negativeOut->channel};
    m_config5711.cfg5711.waveforms.clear();

    m_config5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(positiveOut->channel,
                                QStringLiteral("HighLevelWave"),
                                PXIe5711_make_params({{"amplitude", 10.0}})));

    m_config5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(negativeOut->channel,
                                QStringLiteral("HighLevelWave"),
                                PXIe5711_make_params({{"amplitude", -10.0}})));

    m_configReady = true;
    return true;
}

bool TypicalTpsPlugin::execute(const TPSRequest &request, TPSResult *result, QString *error)
{
    if (!result) {
        if (error) {
            *error = QStringLiteral("result is null");
        }
        return false;
    }

    result->runId = request.runId;
    result->success = true;
    result->summary = QStringLiteral("Typical 温度测试执行完成");
    result->metrics.insert(QStringLiteral("positiveOutputV"), 10.0);
    result->metrics.insert(QStringLiteral("negativeOutputV"), -10.0);
    result->metrics.insert(QStringLiteral("temperatureOnly"), true);
    return true;
}

const TPSPortBinding *TypicalTpsPlugin::findBinding(const QVector<TPSPortBinding> &bindings,
                                                    const QString &identifier)
{
    for (const TPSPortBinding &binding : bindings) {
        if (binding.identifier == identifier) {
            return &binding;
        }
    }
    return nullptr;
}

QString TypicalTpsPlugin::portText(const TPSPortBinding &binding)
{
    const QString resource = binding.resourceId.trimmed().isEmpty()
        ? QStringLiteral("%1.CH%2").arg(deviceKindName(binding.deviceKind)).arg(binding.channel)
        : binding.resourceId;
    return QStringLiteral("%1(%2)").arg(binding.identifier, resource);
}

QString TypicalTpsPlugin::anchorText(const QMap<QString, QVariant> &settings, const QString &key)
{
    const QVariant value = settings.value(key, -1);
    const QString text = value.toString().trimmed();
    return text.isEmpty() ? QStringLiteral("-1") : text;
}
