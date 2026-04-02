#include "exampletpsplugin.h"

#include "../../../IODevices/JYDevices/5711waveformconfig.h"
#include "../../../IODevices/JYDevices/jydeviceconfigutils.h"

ExampleTpsPlugin::ExampleTpsPlugin(QObject *parent)
    : QObject(parent)
{
}

QString ExampleTpsPlugin::pluginId() const
{
    return QStringLiteral("example.tps.basic");
}

QString ExampleTpsPlugin::displayName() const
{
    return QStringLiteral("Example TPS Plugin");
}

QString ExampleTpsPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVector<TPSParamDefinition> ExampleTpsPlugin::parameterDefinitions() const
{
    return requirements().parameters;
}

TPSPluginRequirement ExampleTpsPlugin::requirements() const
{
    TPSPluginRequirement requirement;

    TPSPortRequest voltageOutput;
    voltageOutput.type = TPSPortType::VoltageOutput;
    voltageOutput.count = 1;
    voltageOutput.identifiers = {QStringLiteral("exampleVoltageOutput")};

    TPSPortRequest currentOutput;
    currentOutput.type = TPSPortType::CurrentOutput;
    currentOutput.count = 1;
    currentOutput.identifiers = {QStringLiteral("exampleCurrentOutput")};

    TPSPortRequest voltageInput;
    voltageInput.type = TPSPortType::VoltageInput;
    voltageInput.count = 1;
    voltageInput.identifiers = {QStringLiteral("exampleVoltageInput")};

    TPSPortRequest currentInput;
    currentInput.type = TPSPortType::CurrentInput;
    currentInput.count = 1;
    currentInput.identifiers = {QStringLiteral("exampleCurrentInput")};

    TPSParamDefinition tolerance;
    tolerance.key = QStringLiteral("tolerancePercent");
    tolerance.label = QStringLiteral("检测容差(%)");
    tolerance.type = TPSParamType::Double;
    tolerance.defaultValue = 5.0;
    tolerance.minValue = 0.0;
    tolerance.maxValue = 100.0;
    tolerance.stepValue = 0.1;
    tolerance.unit = QStringLiteral("%");

    TPSParamDefinition currentValue;
    currentValue.key = QStringLiteral("outputCurrentValue");
    currentValue.label = QStringLiteral("输出电流值");
    currentValue.type = TPSParamType::Double;
    currentValue.defaultValue = 0.1;
    currentValue.minValue = -10.0;
    currentValue.maxValue = 10.0;
    currentValue.stepValue = 0.01;
    currentValue.unit = QStringLiteral("A");

        TPSParamDefinition currentPulseDelay;
    currentPulseDelay.key = QStringLiteral("currentPulseTDelay");
    currentPulseDelay.label = QStringLiteral("电流脉冲延迟（s）");
    currentPulseDelay.type = TPSParamType::Double;
    currentPulseDelay.defaultValue = 0.0;
    currentPulseDelay.minValue = 0.0;
    currentPulseDelay.maxValue = 1.0;
    currentPulseDelay.stepValue = 0.0001;
    currentPulseDelay.unit = QStringLiteral("s");

    TPSParamDefinition currentPulseOn;
    currentPulseOn.key = QStringLiteral("currentPulseTOn");
    currentPulseOn.label = QStringLiteral("电流脉冲持续时间（s）");
    currentPulseOn.type = TPSParamType::Double;
    currentPulseOn.defaultValue = 0.2;
    currentPulseOn.minValue = 0.0;
    currentPulseOn.maxValue = 1.0;
    currentPulseOn.stepValue = 0.0001;
    currentPulseOn.unit = QStringLiteral("s");

    TPSParamDefinition currentPulsePeriod;
    currentPulsePeriod.key = QStringLiteral("currentPulseTPeriod");
    currentPulsePeriod.label = QStringLiteral("电流脉冲周期（s）");
    currentPulsePeriod.type = TPSParamType::Double;
    currentPulsePeriod.defaultValue = 1.0;
    currentPulsePeriod.minValue = 0.0001;
    currentPulsePeriod.maxValue = 1.0;
    currentPulsePeriod.stepValue = 0.0001;
    currentPulsePeriod.unit = QStringLiteral("s");

    TPSParamDefinition voltageValue;
    voltageValue.key = QStringLiteral("outputVoltageValue");
    voltageValue.label = QStringLiteral("输出电压值");
    voltageValue.type = TPSParamType::Double;
    voltageValue.defaultValue = 5.0;
    voltageValue.minValue = -10.0;
    voltageValue.maxValue = 10.0;
    voltageValue.stepValue = 0.1;
    voltageValue.unit = QStringLiteral("V");

    TPSParamDefinition voltagePulseDelay;
    voltagePulseDelay.key = QStringLiteral("voltagePulseTDelay");
    voltagePulseDelay.label = QStringLiteral("电压脉冲延迟（s）");
    voltagePulseDelay.type = TPSParamType::Double;
    voltagePulseDelay.defaultValue = 0.0;
    voltagePulseDelay.minValue = 0.0;
    voltagePulseDelay.maxValue = 1.0;
    voltagePulseDelay.stepValue = 0.0001;
    voltagePulseDelay.unit = QStringLiteral("s");

    TPSParamDefinition voltagePulseOn;
    voltagePulseOn.key = QStringLiteral("voltagePulseTOn");
    voltagePulseOn.label = QStringLiteral("电压脉冲持续时间（s）");
    voltagePulseOn.type = TPSParamType::Double;
    voltagePulseOn.defaultValue = 0.2;
    voltagePulseOn.minValue = 0.0;
    voltagePulseOn.maxValue = 1.0;
    voltagePulseOn.stepValue = 0.0001;
    voltagePulseOn.unit = QStringLiteral("s");

    TPSParamDefinition voltagePulsePeriod;
    voltagePulsePeriod.key = QStringLiteral("voltagePulseTPeriod");
    voltagePulsePeriod.label = QStringLiteral("电压脉冲周期（s）");
    voltagePulsePeriod.type = TPSParamType::Double;
    voltagePulsePeriod.defaultValue = 1.0;
    voltagePulsePeriod.minValue = 0.0001;
    voltagePulsePeriod.maxValue = 1.0;
    voltagePulsePeriod.stepValue = 0.0001;
    voltagePulsePeriod.unit = QStringLiteral("s");

    TPSParamDefinition captureMs;
    captureMs.key = QStringLiteral("captureDurationMs");
    captureMs.label = QStringLiteral("采集时长(ms)");
    captureMs.type = TPSParamType::Integer;
    captureMs.defaultValue = 1000;
    captureMs.minValue = 1000;
    captureMs.maxValue = 1000;
    captureMs.stepValue = 0;
    captureMs.unit = QStringLiteral("ms");

    TPSParamDefinition temperatureWarn;
    temperatureWarn.key = QStringLiteral("temperatureWarnC");
    temperatureWarn.label = QStringLiteral("温度报警阈值(℃)");
    temperatureWarn.type = TPSParamType::Double;
    temperatureWarn.defaultValue = 60.0;
    temperatureWarn.minValue = -50.0;
    temperatureWarn.maxValue = 300.0;
    temperatureWarn.stepValue = 0.1;
    temperatureWarn.unit = QStringLiteral("℃");

    TPSParamDefinition temperatureTrip;
    temperatureTrip.key = QStringLiteral("temperatureTripC");
    temperatureTrip.label = QStringLiteral("温度停机阈值(℃)");
    temperatureTrip.type = TPSParamType::Double;
    temperatureTrip.defaultValue = 80.0;
    temperatureTrip.minValue = -50.0;
    temperatureTrip.maxValue = 300.0;
    temperatureTrip.stepValue = 0.1;
    temperatureTrip.unit = QStringLiteral("℃");

    TPSParamDefinition temperatureAnchor;
    temperatureAnchor.key = QStringLiteral("temperatureAnchor");
    temperatureAnchor.label = QStringLiteral("温度区域锚点");
    temperatureAnchor.type = TPSParamType::Integer;
    temperatureAnchor.defaultValue = -1;

    requirement.parameters = {
        tolerance,
        currentValue,
        currentPulseDelay,
        currentPulseOn,
        currentPulsePeriod,
        voltageValue,
        voltagePulseDelay,
        voltagePulseOn,
        voltagePulsePeriod,
        captureMs,
        temperatureWarn,
        temperatureTrip,
        temperatureAnchor
    };
    requirement.ports = {voltageOutput, currentOutput, voltageInput, currentInput};

    return requirement;
}

bool ExampleTpsPlugin::buildDevicePlan(const QVector<TPSPortBinding> &bindings,
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

    const TPSPortBinding *voltageOut = findBinding(bindings, QStringLiteral("exampleVoltageOutput"));
    const TPSPortBinding *currentOut = findBinding(bindings, QStringLiteral("exampleCurrentOutput"));
    const TPSPortBinding *voltageIn = findBinding(bindings, QStringLiteral("exampleVoltageInput"));
    const TPSPortBinding *currentIn = findBinding(bindings, QStringLiteral("exampleCurrentInput"));
    if (!voltageOut || !currentOut || !voltageIn || !currentIn) {
        if (error) {
            *error = QStringLiteral("missing required bindings for example TPS");
        }
        return false;
    }

    TPSDevicePlan devicePlan;
    devicePlan.bindings = bindings;
    devicePlan.cfg532x = build532xInitConfig(JYDeviceKind::PXIe5322);
    devicePlan.cfg5711 = build5711InitConfig();
    devicePlan.cfg8902 = build8902InitConfig();

    const int maxOutputChannel = qMax(voltageOut->channel, currentOut->channel);
    devicePlan.cfg5711.cfg5711.channelCount = maxOutputChannel + 1;
    devicePlan.cfg5711.cfg5711.enabledChannels = {voltageOut->channel, currentOut->channel};
    devicePlan.cfg5711.cfg5711.waveforms.clear();

    const int maxInputChannel = qMax(voltageIn->channel, currentIn->channel);
    devicePlan.cfg532x.cfg532x.channelCount = maxInputChannel + 1;
    devicePlan.cfg532x.cfg532x.sampleRate = 1000.0;
    devicePlan.cfg532x.cfg532x.samplesPerRead = 1000;

    devicePlan.cfg8902.cfg8902.sampleCount = 0;
    devicePlan.cfg8902.cfg8902.slotNumber = -1;

    const double outputVoltage = settings.value(QStringLiteral("outputVoltageValue"), 5.0).toDouble();
    const double outputCurrent = settings.value(QStringLiteral("outputCurrentValue"), 0.1).toDouble();
    const double voltagePulseDelaySec = settings.value(QStringLiteral("voltagePulseTDelay"), 0.0).toDouble();
    const double voltagePulseOnSec = settings.value(QStringLiteral("voltagePulseTOn"), 0.2).toDouble();
    const double voltagePulsePeriodSec = settings.value(QStringLiteral("voltagePulseTPeriod"), 1.0).toDouble();
    const double currentPulseDelaySec = settings.value(QStringLiteral("currentPulseTDelay"), 0.0).toDouble();
    const double currentPulseOnSec = settings.value(QStringLiteral("currentPulseTOn"), 0.2).toDouble();
    const double currentPulsePeriodSec = settings.value(QStringLiteral("currentPulseTPeriod"), 1.0).toDouble();

    devicePlan.cfg5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(voltageOut->channel,
                                QStringLiteral("PulseWave"),
                                PXIe5711_make_params({
                                    {"pulseVLow", 0.0},
                                    {"pulseVHigh", outputVoltage},
                                    {"pulseTDelay", voltagePulseDelaySec},
                                    {"pulseTOn", voltagePulseOnSec},
                                    {"pulseTPeriod", voltagePulsePeriodSec},
                                })));

    devicePlan.cfg5711.cfg5711.waveforms.push_back(
        build5711WaveformConfig(currentOut->channel,
                                QStringLiteral("PulseWave"),
                                PXIe5711_make_params({
                                    {"pulseVLow", 0.0},
                                    {"pulseVHigh", outputCurrent},
                                    {"pulseTDelay", currentPulseDelaySec},
                                    {"pulseTOn", currentPulseOnSec},
                                    {"pulseTPeriod", currentPulsePeriodSec},
                                })));

    TPSSignalRequest voltageOutReq;
    voltageOutReq.id = voltageOut->identifier;
    voltageOutReq.signalType = voltageOut->identifier;
    voltageOutReq.value = outputVoltage;
    voltageOutReq.unit = QStringLiteral("V");
    devicePlan.requests.push_back(voltageOutReq);

    TPSSignalRequest currentOutReq;
    currentOutReq.id = currentOut->identifier;
    currentOutReq.signalType = currentOut->identifier;
    currentOutReq.value = outputCurrent;
    currentOutReq.unit = QStringLiteral("A");
    devicePlan.requests.push_back(currentOutReq);

    TPSSignalRequest voltageInReq;
    voltageInReq.id = voltageIn->identifier;
    voltageInReq.signalType = voltageIn->identifier;
    voltageInReq.unit = QStringLiteral("V");
    devicePlan.requests.push_back(voltageInReq);

    TPSSignalRequest currentInReq;
    currentInReq.id = currentIn->identifier;
    currentInReq.signalType = currentIn->identifier;
    currentInReq.unit = QStringLiteral("A");
    devicePlan.requests.push_back(currentInReq);

    devicePlan.wiringSteps = {
        QStringLiteral("将 %1 连接到输出电压端口").arg(portText(*voltageOut)),
        QStringLiteral("将 %1 连接到输出电流端口").arg(portText(*currentOut)),
        QStringLiteral("将 %1 连接到电压输入端口").arg(portText(*voltageIn)),
        QStringLiteral("将 %1 连接到电流输入端口").arg(portText(*currentIn))
    };

    devicePlan.temperatureGuide = QStringLiteral("将 %1 附近的温度区域作为检测区域")
        .arg(anchorText(settings, QStringLiteral("temperatureAnchor")));
    devicePlan.guide.wiringSteps = devicePlan.wiringSteps;
    devicePlan.guide.roiSteps = {devicePlan.temperatureGuide};
    devicePlan.guide.extensions.insert(QStringLiteral("pluginId"), pluginId());
    devicePlan.guide.extensions.insert(QStringLiteral("wiringFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("temperatureAnchor"))
    });
    devicePlan.guide.extensions.insert(QStringLiteral("roiFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("temperatureAnchor"))
    });

    *plan = devicePlan;
    return true;
}

bool ExampleTpsPlugin::configure(const QMap<QString, QVariant> &settings, QString *error)
{
    Q_UNUSED(error)
    m_settings = settings;
    return true;
}

bool ExampleTpsPlugin::execute(const TPSRequest &request, TPSResult *result, QString *error)
{
    if (!result) {
        if (error) {
            *error = QStringLiteral("result is null");
        }
        return false;
    }

    result->runId = request.runId;
    result->success = true;
    result->summary = QStringLiteral("Example TPS strategy executed");
    result->metrics.insert(QStringLiteral("outputVoltageValue"),
                           m_settings.value(QStringLiteral("outputVoltageValue"), 5.0).toDouble());
    result->metrics.insert(QStringLiteral("outputCurrentValue"),
                           m_settings.value(QStringLiteral("outputCurrentValue"), 0.1).toDouble());
    result->metrics.insert(QStringLiteral("voltagePulseTDelay"),
                           m_settings.value(QStringLiteral("voltagePulseTDelay"), 0.0).toDouble());
    result->metrics.insert(QStringLiteral("voltagePulseTOn"),
                           m_settings.value(QStringLiteral("voltagePulseTOn"), 0.2).toDouble());
    result->metrics.insert(QStringLiteral("voltagePulseTPeriod"),
                           m_settings.value(QStringLiteral("voltagePulseTPeriod"), 1.0).toDouble());
    result->metrics.insert(QStringLiteral("currentPulseTDelay"),
                           m_settings.value(QStringLiteral("currentPulseTDelay"), 0.0).toDouble());
    result->metrics.insert(QStringLiteral("currentPulseTOn"),
                           m_settings.value(QStringLiteral("currentPulseTOn"), 0.2).toDouble());
    result->metrics.insert(QStringLiteral("currentPulseTPeriod"),
                           m_settings.value(QStringLiteral("currentPulseTPeriod"), 1.0).toDouble());
    result->metrics.insert(QStringLiteral("captureDurationMs"), 1000);
    result->metrics.insert(QStringLiteral("temperatureWarnC"),
                           m_settings.value(QStringLiteral("temperatureWarnC"), 80.0).toDouble());
    result->metrics.insert(QStringLiteral("temperatureTripC"),
                           m_settings.value(QStringLiteral("temperatureTripC"), 100.0).toDouble());
    result->metrics.insert(QStringLiteral("items"), request.items.size());
    return true;
}

const TPSPortBinding *ExampleTpsPlugin::findBinding(const QVector<TPSPortBinding> &bindings, const QString &identifier)
{
    for (const TPSPortBinding &binding : bindings) {
        if (binding.identifier == identifier) {
            return &binding;
        }
    }
    return nullptr;
}

QString ExampleTpsPlugin::deviceKindName(JYDeviceKind kind)
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

QString ExampleTpsPlugin::portText(const TPSPortBinding &binding)
{
    const QString resource = binding.resourceId.trimmed().isEmpty()
        ? QStringLiteral("%1.CH%2").arg(deviceKindName(binding.deviceKind)).arg(binding.channel)
        : binding.resourceId;
    return QStringLiteral("%1(%2)").arg(binding.identifier, resource);
}

QString ExampleTpsPlugin::anchorText(const QMap<QString, QVariant> &settings, const QString &key)
{
    const QVariant value = settings.value(key, -1);
    const QString text = value.toString().trimmed();
    return text.isEmpty() ? QStringLiteral("-1") : text;
}
