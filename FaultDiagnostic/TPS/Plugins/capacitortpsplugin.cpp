#include "capacitortpsplugin.h"

#include "../Core/tpsruntimecontext.h"

#include "../../../IODevices/JYDevices/5711waveformconfig.h"
#include "../../../IODevices/JYDevices/jydeviceconfigutils.h"

#include <QRegularExpression>

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

QString portText(const TPSPortBinding &binding)
{
    const QString resource = binding.resourceId.trimmed().isEmpty()
        ? QStringLiteral("%1.CH%2").arg(deviceKindName(binding.deviceKind)).arg(binding.channel)
        : binding.resourceId;
    return QStringLiteral("%1(%2)").arg(binding.identifier, resource);
}

QString anchorText(const QMap<QString, QVariant> &settings, const QString &key)
{
    const QVariant value = settings.value(key, -1);
    const QString text = value.toString().trimmed();
    return text.isEmpty() ? QStringLiteral("-1") : text;
}

int resolveModeValue(const QVariant &value, int fallback)
{
    bool ok = false;
    const int direct = value.toInt(&ok);
    if (ok) {
        return qBound(0, direct, 4);
    }

    const QString text = value.toString().trimmed().toUpper();
    const QRegularExpression re(QStringLiteral("MODE\\s*([0-4])"));
    const QRegularExpressionMatch match = re.match(text);
    if (match.hasMatch()) {
        return match.captured(1).toInt();
    }

    return fallback;
}
}

CapacitorTpsPlugin::CapacitorTpsPlugin(QObject *parent)
    : QObject(parent)
{
}

QString CapacitorTpsPlugin::pluginId() const
{
    return QStringLiteral("tps.capacitor");
}

QString CapacitorTpsPlugin::displayName() const
{
    return QStringLiteral("Capacitor MODE TPS");
}

QString CapacitorTpsPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVector<TPSParamDefinition> CapacitorTpsPlugin::parameterDefinitions() const
{
    return requirements().parameters;
}

TPSPluginRequirement CapacitorTpsPlugin::requirements() const
{
    TPSPluginRequirement requirement;

    TPSParamDefinition sampleRate;
    sampleRate.key = QStringLiteral("sampleRateHz");
    sampleRate.label = QStringLiteral("采样率(Hz)");
    sampleRate.type = TPSParamType::Double;
    sampleRate.defaultValue = 10000.0;
    sampleRate.minValue = 1.0;
    sampleRate.maxValue = 10e6;
    sampleRate.stepValue = 1.0;

    TPSParamDefinition captureMs;
    captureMs.key = QStringLiteral("captureDurationMs");
    captureMs.label = QStringLiteral("采集时长(ms)");
    captureMs.type = TPSParamType::Integer;
    captureMs.defaultValue = 1000;
    captureMs.minValue = 1;
    captureMs.maxValue = 60000;
    captureMs.stepValue = 10;

    TPSParamDefinition cNominal;
    cNominal.key = QStringLiteral("cNominal_uF");
    cNominal.label = QStringLiteral("标称电容(μF)");
    cNominal.type = TPSParamType::Double;
    cNominal.defaultValue = 10.0;
    cNominal.minValue = 0.001;
    cNominal.maxValue = 100000.0;
    cNominal.stepValue = 0.1;

    TPSParamDefinition rSeries;
    rSeries.key = QStringLiteral("r1Ohms");
    rSeries.label = QStringLiteral("串联电阻R1(Ω)");
    rSeries.type = TPSParamType::Double;
    rSeries.defaultValue = 100.0;
    rSeries.minValue = 0.1;
    rSeries.maxValue = 1e6;
    rSeries.stepValue = 1.0;

    TPSParamDefinition modeTolerance;
    modeTolerance.key = QStringLiteral("tauDriftTolerance");
    modeTolerance.label = QStringLiteral("时间常数漂移容差");
    modeTolerance.type = TPSParamType::Double;
    modeTolerance.defaultValue = 0.35;
    modeTolerance.minValue = 0.01;
    modeTolerance.maxValue = 5.0;
    modeTolerance.stepValue = 0.01;

    TPSParamDefinition outAmplitude;
    outAmplitude.key = QStringLiteral("stimulusAmplitudeV");
    outAmplitude.label = QStringLiteral("激励高电平幅值(V)");
    outAmplitude.type = TPSParamType::Double;
    outAmplitude.defaultValue = 2.0;
    outAmplitude.minValue = 0.01;
    outAmplitude.maxValue = 10.0;
    outAmplitude.stepValue = 0.1;

    TPSParamDefinition outFrequency;
    outFrequency.key = QStringLiteral("stimulusFrequencyHz");
    outFrequency.label = QStringLiteral("激励频率(Hz)");
    outFrequency.type = TPSParamType::Double;
    outFrequency.defaultValue = 100.0;
    outFrequency.minValue = 0.1;
    outFrequency.maxValue = 1e6 / 2.0; // 香农频率上限 不能超过采样率的一半
    outFrequency.stepValue = 0.1;

    TPSParamDefinition inputVcapPositiveAnchor;
    inputVcapPositiveAnchor.key = QStringLiteral("vcapPositiveAnchor");
    inputVcapPositiveAnchor.label = QStringLiteral("Vcap正极锚点");
    inputVcapPositiveAnchor.type = TPSParamType::Integer;
    inputVcapPositiveAnchor.defaultValue = -1;

    TPSParamDefinition inputVcapNegativeAnchor;
    inputVcapNegativeAnchor.key = QStringLiteral("vcapNegativeAnchor");
    inputVcapNegativeAnchor.label = QStringLiteral("Vcap负极锚点");
    inputVcapNegativeAnchor.type = TPSParamType::Integer;
    inputVcapNegativeAnchor.defaultValue = -1;

    TPSParamDefinition inputVinPositiveAnchor;
    inputVinPositiveAnchor.key = QStringLiteral("vinPositiveAnchor");
    inputVinPositiveAnchor.label = QStringLiteral("Vin正极锚点");
    inputVinPositiveAnchor.type = TPSParamType::Integer;
    inputVinPositiveAnchor.defaultValue = -1;

    TPSParamDefinition inputVinNegativeAnchor;
    inputVinNegativeAnchor.key = QStringLiteral("vinNegativeAnchor");
    inputVinNegativeAnchor.label = QStringLiteral("Vin负极锚点");
    inputVinNegativeAnchor.type = TPSParamType::Integer;
    inputVinNegativeAnchor.defaultValue = -1;

    TPSParamDefinition outputVinAnchor;
    outputVinAnchor.key = QStringLiteral("outputVinAnchor");
    outputVinAnchor.label = QStringLiteral("输出正极锚点");
    outputVinAnchor.type = TPSParamType::Integer;
    outputVinAnchor.defaultValue = -1;

    TPSParamDefinition outputGroundAnchor;
    outputGroundAnchor.key = QStringLiteral("outputGroundAnchor");
    outputGroundAnchor.label = QStringLiteral("输出参考地锚点");
    outputGroundAnchor.type = TPSParamType::Integer;
    outputGroundAnchor.defaultValue = -1;

    TPSParamDefinition temperatureAnchor;
    temperatureAnchor.key = QStringLiteral("temperatureAnchor");
    temperatureAnchor.label = QStringLiteral("温度区域锚点");
    temperatureAnchor.type = TPSParamType::Integer;
    temperatureAnchor.defaultValue = -1;

    TPSParamDefinition temperatureWarn;
    temperatureWarn.key = QStringLiteral("temperatureWarnC");
    temperatureWarn.label = QStringLiteral("温度预警阈值(℃)");
    temperatureWarn.type = TPSParamType::Double;
    temperatureWarn.defaultValue = 60.0;
    temperatureWarn.minValue = -40.0;
    temperatureWarn.maxValue = 250.0;
    temperatureWarn.stepValue = 0.1;

    TPSParamDefinition temperatureTrip;
    temperatureTrip.key = QStringLiteral("temperatureTripC");
    temperatureTrip.label = QStringLiteral("温度停机阈值(℃)");
    temperatureTrip.type = TPSParamType::Double;
    temperatureTrip.defaultValue = 80.0;
    temperatureTrip.minValue = -40.0;
    temperatureTrip.maxValue = 250.0;
    temperatureTrip.stepValue = 0.1;

    TPSPortRequest outputReq;
    outputReq.type = TPSPortType::VoltageOutput;
    outputReq.count = 1;
    outputReq.identifiers = {QStringLiteral("capacitorStimulusOutput")};

    TPSPortRequest inputReq;
    inputReq.type = TPSPortType::VoltageInput;
    inputReq.count = 2;
    inputReq.identifiers = {QStringLiteral("capVoltageInput"), QStringLiteral("vinVoltageInput")};

    requirement.parameters = {
        cNominal,
        rSeries,
        captureMs,
        modeTolerance,
        outAmplitude,
        outFrequency,
        inputVcapPositiveAnchor,
        inputVcapNegativeAnchor,
        inputVinPositiveAnchor,
        inputVinNegativeAnchor,
        outputVinAnchor,
        outputGroundAnchor,
        temperatureWarn,
        temperatureTrip,
        temperatureAnchor
    };
    requirement.ports = {outputReq, inputReq};

    return requirement;
}

bool CapacitorTpsPlugin::buildDevicePlan(const QVector<TPSPortBinding> &bindings,
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

    const TPSPortBinding *output = findBinding(effectiveBindings, QStringLiteral("capacitorStimulusOutput"));
    const TPSPortBinding *vcapInput = findBinding(effectiveBindings, QStringLiteral("capVoltageInput"));
    const TPSPortBinding *vinInput = findBinding(effectiveBindings, QStringLiteral("vinVoltageInput"));
    if (!output || !vcapInput || !vinInput) {
        if (error) {
            *error = QStringLiteral("missing required bindings: capacitorStimulusOutput/capVoltageInput/vinVoltageInput");
        }
        return false;
    }

    const bool vcapOn5322 = (vcapInput->deviceKind == JYDeviceKind::PXIe5322);
    const bool vinOn5322 = (vinInput->deviceKind == JYDeviceKind::PXIe5322);
    if (!vcapOn5322 || !vinOn5322) {
        if (error) {
            *error = QStringLiteral("capVoltageInput and vinVoltageInput must both bind to PXIe5322");
        }
        return false;
    }
    if (vcapInput->deviceKind != vinInput->deviceKind) {
        if (error) {
            *error = QStringLiteral("capVoltageInput and vinVoltageInput must be on the same capture card");
        }
        return false;
    }

    TPSDevicePlan devicePlan;
    devicePlan.bindings = effectiveBindings;
    devicePlan.cfg532x = build532xInitConfig(vcapInput->deviceKind);
    devicePlan.cfg5711 = build5711InitConfig();
    devicePlan.cfg8902 = build8902InitConfig();

    devicePlan.cfg532x.cfg532x.channelCount = qMax(vcapInput->channel, vinInput->channel) + 1;
    devicePlan.cfg5711.cfg5711.channelCount = output->channel + 1;
    devicePlan.cfg5711.cfg5711.enabledChannels = {output->channel};
    devicePlan.cfg5711.cfg5711.waveforms.clear();
    devicePlan.cfg8902.cfg8902.sampleCount = 0;
    devicePlan.cfg8902.cfg8902.slotNumber = -1;

    const double amplitude = settings.value(QStringLiteral("stimulusAmplitudeV"), 2.0).toDouble();
    const double frequency = settings.value(QStringLiteral("stimulusFrequencyHz"), 100.0).toDouble();

    devicePlan.cfg5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(output->channel,
                                QStringLiteral("HighLevelWave"),
                                PXIe5711_make_params({
                                    {"amplitude", amplitude},
                                    {"frequency", frequency},
                                })));

    TPSSignalRequest outReq;
    outReq.id = output->identifier;
    outReq.signalType = output->identifier;
    outReq.value = amplitude;
    outReq.unit = QStringLiteral("V");
    devicePlan.requests.push_back(outReq);

    TPSSignalRequest capReq;
    capReq.id = vcapInput->identifier;
    capReq.signalType = vcapInput->identifier;
    capReq.unit = QStringLiteral("V");
    devicePlan.requests.push_back(capReq);

    TPSSignalRequest vinReq;
    vinReq.id = vinInput->identifier;
    vinReq.signalType = vinInput->identifier;
    vinReq.unit = QStringLiteral("V");
    devicePlan.requests.push_back(vinReq);

    devicePlan.wiringSteps = {
        QStringLiteral("将%1正端接到%2").arg(portText(*output), anchorText(settings, QStringLiteral("outputVinAnchor"))),
        QStringLiteral("将%1负端接到%2").arg(portText(*output), anchorText(settings, QStringLiteral("outputGroundAnchor"))),
        QStringLiteral("将%1正极接到%2").arg(portText(*vcapInput), anchorText(settings, QStringLiteral("vcapPositiveAnchor"))),
        QStringLiteral("将%1负极接到%2").arg(portText(*vcapInput), anchorText(settings, QStringLiteral("vcapNegativeAnchor"))),
        QStringLiteral("将%1正极接到%2").arg(portText(*vinInput), anchorText(settings, QStringLiteral("vinPositiveAnchor"))),
        QStringLiteral("将%1负极接到%2").arg(portText(*vinInput), anchorText(settings, QStringLiteral("vinNegativeAnchor")))
    };

    devicePlan.temperatureGuide = QStringLiteral("请在%1完成电容及限流电阻周边测温ROI框选")
        .arg(anchorText(settings, QStringLiteral("temperatureAnchor")));

    devicePlan.guide.wiringSteps = devicePlan.wiringSteps;
    devicePlan.guide.roiSteps = {devicePlan.temperatureGuide};
    devicePlan.guide.extensions.insert(QStringLiteral("pluginId"), pluginId());
    devicePlan.guide.extensions.insert(QStringLiteral("mode"), expectedMode());
    devicePlan.guide.extensions.insert(QStringLiteral("wiringFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("outputVinAnchor")),
        anchorText(settings, QStringLiteral("outputGroundAnchor")),
        anchorText(settings, QStringLiteral("vcapPositiveAnchor")),
        anchorText(settings, QStringLiteral("vcapNegativeAnchor")),
        anchorText(settings, QStringLiteral("vinPositiveAnchor")),
        anchorText(settings, QStringLiteral("vinNegativeAnchor"))
    });
    devicePlan.guide.extensions.insert(QStringLiteral("roiFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("temperatureAnchor"))
    });

    *plan = devicePlan;
    return true;
}

bool CapacitorTpsPlugin::configure(const QMap<QString, QVariant> &settings, QString *error)
{
    Q_UNUSED(error)
    m_settings = settings;
    m_allocatedBindings = TPSRuntimeContext::decodeBindings(settings.value(TPSRuntimeContext::allocatedBindingsKey()));

    const TPSPortBinding *output = findBinding(m_allocatedBindings, QStringLiteral("capacitorStimulusOutput"));
    if (!output) {
        m_configReady = false;
        return false;
    }

    const double amplitude = settings.value(QStringLiteral("stimulusAmplitudeV"), 2.0).toDouble();
    const double frequency = settings.value(QStringLiteral("stimulusFrequencyHz"), 100.0).toDouble();

    m_config5711 = build5711InitConfig();
    m_config5711.cfg5711.channelCount = output->channel + 1;
    m_config5711.cfg5711.enabledChannels = {output->channel};
    m_config5711.cfg5711.waveforms.clear();

    m_config5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(output->channel,
                                QStringLiteral("HighLevelWave"),
                                PXIe5711_make_params({
                                    {"amplitude", amplitude},
                                    {"frequency", frequency},
                                })));

    m_configReady = true;
    return true;
}

bool CapacitorTpsPlugin::execute(const TPSRequest &request, TPSResult *result, QString *error)
{
    if (!result) {
        if (error) {
            *error = QStringLiteral("result is null");
        }
        return false;
    }

    result->runId = request.runId;
    result->success = true;
    result->summary = QStringLiteral("Capacitor TPS strategy executed");
    result->metrics.insert(QStringLiteral("expectedMode"), expectedMode());
    result->metrics.insert(QStringLiteral("captureDurationMs"),
                           m_settings.value(QStringLiteral("captureDurationMs"), 1000).toInt());
    result->metrics.insert(QStringLiteral("sampleRateHz"),
                           m_settings.value(QStringLiteral("sampleRateHz"), 10000.0).toDouble());
    result->metrics.insert(QStringLiteral("stimulusAmplitudeV"),
                           m_settings.value(QStringLiteral("stimulusAmplitudeV"), 2.0).toDouble());
    result->metrics.insert(QStringLiteral("stimulusFrequencyHz"),
                           m_settings.value(QStringLiteral("stimulusFrequencyHz"), 100.0).toDouble());

    return true;
}

int CapacitorTpsPlugin::expectedMode() const
{
    return resolveModeValue(m_settings.value(QStringLiteral("expectedMode")), 0);
}

const TPSPortBinding *CapacitorTpsPlugin::findBinding(const QVector<TPSPortBinding> &bindings, const QString &identifier)
{
    for (const auto &binding : bindings) {
        if (binding.identifier == identifier) {
            return &binding;
        }
    }
    return nullptr;
}
