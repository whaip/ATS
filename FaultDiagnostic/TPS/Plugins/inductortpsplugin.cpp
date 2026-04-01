#include "inductortpsplugin.h"

#include "../Core/tpsruntimecontext.h"

#include "../../../IODevices/JYDevices/5711waveformconfig.h"
#include "../../../IODevices/JYDevices/jydeviceconfigutils.h"


namespace {
constexpr double kFixedStimulusAmplitudeV = 5.0;
constexpr double kFixedStimulusFrequencyHz = 250.0;
constexpr double kFixedDutyCycle = 0.5;
constexpr double kFixedCaptureSampleRateHz = 1000000.0;
constexpr int kFixedCaptureDurationMs = 500;
constexpr double kFixedSeriesResistorOhms = 10.0;

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

InductorTpsPlugin::InductorTpsPlugin(QObject *parent)
    : QObject(parent)
{
}

QString InductorTpsPlugin::pluginId() const
{
    return QStringLiteral("tps.inductor");
}

QString InductorTpsPlugin::displayName() const
{
    return QStringLiteral("Inductor Dynamic TPS");
}

QString InductorTpsPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVector<TPSParamDefinition> InductorTpsPlugin::parameterDefinitions() const
{
    return requirements().parameters;
}

TPSPluginRequirement InductorTpsPlugin::requirements() const
{
    TPSPluginRequirement requirement;

    TPSParamDefinition nominalL;
    nominalL.key = QStringLiteral("nominalL_uH");
    nominalL.label = QStringLiteral("标称电感(μH)");
    nominalL.type = TPSParamType::Double;
    nominalL.defaultValue = 100.0;
    nominalL.minValue = 0.001;
    nominalL.maxValue = 1e9;
    nominalL.stepValue = 0.1;

    TPSParamDefinition tolerance;
    tolerance.key = QStringLiteral("tolerancePercent");
    tolerance.label = QStringLiteral("容差(%)");
    tolerance.type = TPSParamType::Double;
    tolerance.defaultValue = 10.0;
    tolerance.minValue = 0.0;
    tolerance.maxValue = 100.0;
    tolerance.stepValue = 0.1;

    TPSParamDefinition vinAnchor;
    vinAnchor.key = QStringLiteral("vinAnchor");
    vinAnchor.label = QStringLiteral("Vin锚点");
    vinAnchor.type = TPSParamType::Integer;
    vinAnchor.defaultValue = -1;

    TPSParamDefinition vn1Anchor;
    vn1Anchor.key = QStringLiteral("vn1Anchor");
    vn1Anchor.label = QStringLiteral("Vn1锚点");
    vn1Anchor.type = TPSParamType::Integer;
    vn1Anchor.defaultValue = -1;

    TPSParamDefinition vn2Anchor;
    vn2Anchor.key = QStringLiteral("vn2Anchor");
    vn2Anchor.label = QStringLiteral("Vn2锚点");
    vn2Anchor.type = TPSParamType::Integer;
    vn2Anchor.defaultValue = -1;

    TPSParamDefinition outputVinAnchor;
    outputVinAnchor.key = QStringLiteral("outputVinAnchor");
    outputVinAnchor.label = QStringLiteral("激励输出锚点");
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
    outputReq.identifiers = {QStringLiteral("inductorStimulusOutput")};

    TPSPortRequest vinReq;
    vinReq.type = TPSPortType::VoltageInput;
    vinReq.count = 1;
    vinReq.identifiers = {QStringLiteral("inductorVinInput")};

    TPSPortRequest vn1Req;
    vn1Req.type = TPSPortType::VoltageInput;
    vn1Req.count = 1;
    vn1Req.identifiers = {QStringLiteral("inductorVn1Input")};

    TPSPortRequest vn2Req;
    vn2Req.type = TPSPortType::VoltageInput;
    vn2Req.count = 1;
    vn2Req.identifiers = {QStringLiteral("inductorVn2Input")};

    requirement.parameters = {
        nominalL,
        tolerance,
        vinAnchor,
        vn1Anchor,
        vn2Anchor,
        outputVinAnchor,
        outputGroundAnchor,
        temperatureWarn,
        temperatureTrip,
        temperatureAnchor
    };
    requirement.ports = {outputReq, vinReq, vn1Req, vn2Req};

    return requirement;
}

bool InductorTpsPlugin::buildDevicePlan(const QVector<TPSPortBinding> &bindings,
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

    const TPSPortBinding *output = findBinding(effectiveBindings, QStringLiteral("inductorStimulusOutput"));
    const TPSPortBinding *vinInput = findBinding(effectiveBindings, QStringLiteral("inductorVinInput"));
    const TPSPortBinding *vn1Input = findBinding(effectiveBindings, QStringLiteral("inductorVn1Input"));
    const TPSPortBinding *vn2Input = findBinding(effectiveBindings, QStringLiteral("inductorVn2Input"));
    if (!output || !vinInput || !vn1Input || !vn2Input) {
        if (error) {
            *error = QStringLiteral("missing required bindings: inductorStimulusOutput/inductorVinInput/inductorVn1Input/inductorVn2Input");
        }
        return false;
    }

    const bool vinOn532x = (vinInput->deviceKind == JYDeviceKind::PXIe5322 || vinInput->deviceKind == JYDeviceKind::PXIe5323);
    const bool vn1On532x = (vn1Input->deviceKind == JYDeviceKind::PXIe5322 || vn1Input->deviceKind == JYDeviceKind::PXIe5323);
    const bool vn2On532x = (vn2Input->deviceKind == JYDeviceKind::PXIe5322 || vn2Input->deviceKind == JYDeviceKind::PXIe5323);
    if (!vinOn532x || !vn1On532x || !vn2On532x) {
        if (error) {
            *error = QStringLiteral("inductorVinInput/inductorVn1Input/inductorVn2Input must bind to PXIe5322 or PXIe5323");
        }
        return false;
    }

    if (vinInput->deviceKind != vn1Input->deviceKind || vinInput->deviceKind != vn2Input->deviceKind) {
        if (error) {
            *error = QStringLiteral("inductorVinInput/inductorVn1Input/inductorVn2Input must be on the same capture card");
        }
        return false;
    }

    TPSDevicePlan devicePlan;
    devicePlan.bindings = effectiveBindings;
    devicePlan.cfg532x = build532xInitConfig(vinInput->deviceKind);
    devicePlan.cfg5711 = build5711InitConfig();
    devicePlan.cfg8902 = build8902InitConfig();

    devicePlan.cfg532x.cfg532x.channelCount = qMax(vinInput->channel, qMax(vn1Input->channel, vn2Input->channel)) + 1;
    devicePlan.cfg532x.cfg532x.sampleRate = kFixedCaptureSampleRateHz;
    devicePlan.cfg5711.cfg5711.channelCount = output->channel + 1;
    devicePlan.cfg5711.cfg5711.sampleRate = kFixedCaptureSampleRateHz;
    devicePlan.cfg5711.cfg5711.enabledChannels = {output->channel};
    devicePlan.cfg5711.cfg5711.waveforms.clear();
    devicePlan.cfg8902.cfg8902.sampleCount = 0;
    devicePlan.cfg8902.cfg8902.slotNumber = -1;

    devicePlan.cfg5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(output->channel,
                                QStringLiteral("SquareWave"),
                                PXIe5711_make_params({
                                    {"amplitude", kFixedStimulusAmplitudeV},
                                    {"frequency", kFixedStimulusFrequencyHz},
                                    {"dutyCycle", kFixedDutyCycle},
                                })));

    TPSSignalRequest outputReq;
    outputReq.id = output->identifier;
    outputReq.signalType = output->identifier;
    outputReq.value = kFixedStimulusAmplitudeV;
    outputReq.unit = QStringLiteral("V");
    devicePlan.requests.push_back(outputReq);

    TPSSignalRequest vinReq;
    vinReq.id = vinInput->identifier;
    vinReq.signalType = vinInput->identifier;
    vinReq.unit = QStringLiteral("V");
    devicePlan.requests.push_back(vinReq);

    TPSSignalRequest vn1Req;
    vn1Req.id = vn1Input->identifier;
    vn1Req.signalType = vn1Input->identifier;
    vn1Req.unit = QStringLiteral("V");
    devicePlan.requests.push_back(vn1Req);

    TPSSignalRequest vn2Req;
    vn2Req.id = vn2Input->identifier;
    vn2Req.signalType = vn2Input->identifier;
    vn2Req.unit = QStringLiteral("V");
    devicePlan.requests.push_back(vn2Req);

    devicePlan.wiringSteps = {
        QStringLiteral("将%1正端接到%2").arg(portText(*output), anchorText(settings, QStringLiteral("outputVinAnchor"))),
        QStringLiteral("将%1负端接到%2").arg(portText(*output), anchorText(settings, QStringLiteral("outputGroundAnchor"))),
        QStringLiteral("将%1测量端接到%2").arg(portText(*vinInput), anchorText(settings, QStringLiteral("vinAnchor"))),
        QStringLiteral("将%1测量端接到%2").arg(portText(*vn1Input), anchorText(settings, QStringLiteral("vn1Anchor"))),
        QStringLiteral("将%1测量端接到%2").arg(portText(*vn2Input), anchorText(settings, QStringLiteral("vn2Anchor")))
    };

    devicePlan.temperatureGuide = QStringLiteral("请在%1完成电感本体与夹具接触点测温ROI框选")
        .arg(anchorText(settings, QStringLiteral("temperatureAnchor")));

    devicePlan.guide.wiringSteps = devicePlan.wiringSteps;
    devicePlan.guide.roiSteps = {devicePlan.temperatureGuide};
    devicePlan.guide.extensions.insert(QStringLiteral("pluginId"), pluginId());
    devicePlan.guide.extensions.insert(QStringLiteral("wiringFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("outputVinAnchor")),
        anchorText(settings, QStringLiteral("outputGroundAnchor")),
        anchorText(settings, QStringLiteral("vinAnchor")),
        anchorText(settings, QStringLiteral("vn1Anchor")),
        anchorText(settings, QStringLiteral("vn2Anchor"))
    });
    devicePlan.guide.extensions.insert(QStringLiteral("roiFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("temperatureAnchor"))
    });
    devicePlan.guide.extensions.insert(QStringLiteral("fixed.stimulusAmplitudeV"), kFixedStimulusAmplitudeV);
    devicePlan.guide.extensions.insert(QStringLiteral("fixed.stimulusFrequencyHz"), kFixedStimulusFrequencyHz);
    devicePlan.guide.extensions.insert(QStringLiteral("fixed.captureSampleRateHz"), kFixedCaptureSampleRateHz);
    devicePlan.guide.extensions.insert(QStringLiteral("fixed.captureDurationMs"), kFixedCaptureDurationMs);
    devicePlan.guide.extensions.insert(QStringLiteral("fixed.rsOhms"), kFixedSeriesResistorOhms);

    *plan = devicePlan;
    return true;
}

bool InductorTpsPlugin::configure(const QMap<QString, QVariant> &settings, QString *error)
{
    m_settings = settings;
    m_allocatedBindings = TPSRuntimeContext::decodeBindings(settings.value(TPSRuntimeContext::allocatedBindingsKey()));

    const TPSPortBinding *output = findBinding(m_allocatedBindings, QStringLiteral("inductorStimulusOutput"));
    if (!output) {
        m_configReady = false;
        if (error) {
            *error = QStringLiteral("missing allocated binding: inductorStimulusOutput");
        }
        return false;
    }

    if (output->deviceKind != JYDeviceKind::PXIe5711) {
        m_configReady = false;
        if (error) {
            *error = QStringLiteral("inductorStimulusOutput must bind to PXIe5711");
        }
        return false;
    }

    m_config5711 = build5711InitConfig();
    m_config5711.cfg5711.channelCount = output->channel + 1;
    m_config5711.cfg5711.sampleRate = kFixedCaptureSampleRateHz;
    m_config5711.cfg5711.enabledChannels = {output->channel};
    m_config5711.cfg5711.waveforms.clear();

    m_config5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(output->channel,
                                QStringLiteral("SquareWave"),
                                PXIe5711_make_params({
                                    {"amplitude", kFixedStimulusAmplitudeV},
                                    {"frequency", kFixedStimulusFrequencyHz},
                                    {"dutyCycle", kFixedDutyCycle},
                                })));

    m_configReady = true;
    return true;
}

bool InductorTpsPlugin::execute(const TPSRequest &request, TPSResult *result, QString *error)
{
    if (!result) {
        if (error) {
            *error = QStringLiteral("result is null");
        }
        return false;
    }

    result->runId = request.runId;
    result->success = true;
    result->summary = QStringLiteral("Inductor TPS strategy executed");
    result->metrics.insert(QStringLiteral("nominalL_uH"),
                           m_settings.value(QStringLiteral("nominalL_uH"), 100.0).toDouble());
    result->metrics.insert(QStringLiteral("tolerancePercent"),
                           m_settings.value(QStringLiteral("tolerancePercent"), 10.0).toDouble());
    result->metrics.insert(QStringLiteral("sampleRateHz"), kFixedCaptureSampleRateHz);
    result->metrics.insert(QStringLiteral("captureDurationMs"), kFixedCaptureDurationMs);
    result->metrics.insert(QStringLiteral("stimulusAmplitudeV"), kFixedStimulusAmplitudeV);
    result->metrics.insert(QStringLiteral("stimulusFrequencyHz"), kFixedStimulusFrequencyHz);
    result->metrics.insert(QStringLiteral("seriesResistorOhms"), kFixedSeriesResistorOhms);

    return true;
}





const TPSPortBinding *InductorTpsPlugin::findBinding(const QVector<TPSPortBinding> &bindings, const QString &identifier)
{
    for (const TPSPortBinding &binding : bindings) {
        if (binding.identifier == identifier) {
            return &binding;
        }
    }
    return nullptr;
}

QString InductorTpsPlugin::portText(const TPSPortBinding &binding)
{
    const QString resource = binding.resourceId.trimmed().isEmpty()
        ? QStringLiteral("%1.CH%2").arg(deviceKindName(binding.deviceKind)).arg(binding.channel)
        : binding.resourceId;
    return QStringLiteral("%1(%2)").arg(binding.identifier, resource);
}

QString InductorTpsPlugin::anchorText(const QMap<QString, QVariant> &settings, const QString &key)
{
    const QVariant value = settings.value(key, -1);
    const QString text = value.toString().trimmed();
    return text.isEmpty() ? QStringLiteral("-1") : text;
}




