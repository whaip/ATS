#include "multitpsplugin.h"

#include "../../../IODevices/JYDevices/5711waveformconfig.h"
#include "../../../IODevices/JYDevices/jydeviceconfigutils.h"

namespace {
PXIe5711_testtype waveformFromString(const QString &value)
{
    for (const auto &option : PXIe5711_waveform_options()) {
        if (PXIe5711_testtype_to_string(option).compare(value, Qt::CaseInsensitive) == 0) {
            return option;
        }
    }
    return PXIe5711_testtype::HighLevelWave;
}
}

MultiSignalTpsPlugin::MultiSignalTpsPlugin(QObject *parent)
    : QObject(parent)
{
}

QString MultiSignalTpsPlugin::pluginId() const
{
    return QStringLiteral("tps.multi.signal");
}

QString MultiSignalTpsPlugin::displayName() const
{
    return QStringLiteral("Multi Signal TPS");
}

QString MultiSignalTpsPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVector<TPSParamDefinition> MultiSignalTpsPlugin::parameterDefinitions() const
{
    return requirements().parameters;
}

TPSPluginRequirement MultiSignalTpsPlugin::requirements() const
{
    TPSPluginRequirement requirement;

    TPSPortRequest currentOut;
    currentOut.type = TPSPortType::CurrentOutput;
    currentOut.count = 1;
    currentOut.identifiers = {QStringLiteral("currentOut1")};

    TPSPortRequest voltageOut;
    voltageOut.type = TPSPortType::VoltageOutput;
    voltageOut.count = 2;
    voltageOut.identifiers = {QStringLiteral("voltageOut1"), QStringLiteral("voltageOut2")};

    TPSPortRequest currentIn;
    currentIn.type = TPSPortType::CurrentInput;
    currentIn.count = 1;
    currentIn.identifiers = {QStringLiteral("currentIn1")};

    TPSPortRequest voltageIn;
    voltageIn.type = TPSPortType::VoltageInput;
    voltageIn.count = 2;
    voltageIn.identifiers = {QStringLiteral("voltageIn1"), QStringLiteral("voltageIn2")};

    requirement.ports = {currentOut, voltageOut, currentIn, voltageIn};

    TPSParamDefinition mode;
    mode.key = QStringLiteral("mode");
    mode.label = QStringLiteral("采集模式");
    mode.type = TPSParamType::Enum;
    mode.enumOptions = {QStringLiteral("并行"), QStringLiteral("顺序")};
    mode.defaultValue = mode.enumOptions.value(0);
    requirement.parameters.push_back(mode);

    TPSParamDefinition tolerance;
    tolerance.key = QStringLiteral("tolerancePercent");
    tolerance.label = QStringLiteral("容差(%)");
    tolerance.type = TPSParamType::Double;
    tolerance.defaultValue = 5.0;
    tolerance.minValue = 0.0;
    tolerance.maxValue = 100.0;
    tolerance.stepValue = 0.1;
    requirement.parameters.push_back(tolerance);

    const QVector<PXIe5711_testtype> waveformOptions = PXIe5711_waveform_options();
    QStringList waveformNames;
    waveformNames.reserve(waveformOptions.size());
    for (const auto &waveform : waveformOptions) {
        waveformNames.push_back(PXIe5711_testtype_to_string(waveform));
    }

    auto addPortParams = [&](const QString &identifier,
                             const QString &labelPrefix,
                             const QString &unit) {
        TPSParamDefinition waveform;
        waveform.key = QStringLiteral("%1.waveform").arg(identifier);
        waveform.label = QStringLiteral("%1波形").arg(labelPrefix);
        waveform.type = TPSParamType::Enum;
        waveform.enumOptions = waveformNames;
        waveform.defaultValue = waveform.enumOptions.value(0);

        TPSParamDefinition amplitude;
        amplitude.key = QStringLiteral("%1.amplitude").arg(identifier);
        amplitude.label = QStringLiteral("%1幅值").arg(labelPrefix);
        amplitude.type = TPSParamType::Double;
        amplitude.defaultValue = 1.0;
        amplitude.minValue = 0.0;
        amplitude.maxValue = 10.0;
        amplitude.stepValue = 0.1;
        amplitude.unit = unit;

        TPSParamDefinition frequency;
        frequency.key = QStringLiteral("%1.frequency").arg(identifier);
        frequency.label = QStringLiteral("%1频率").arg(labelPrefix);
        frequency.type = TPSParamType::Double;
        frequency.defaultValue = 1000.0;
        frequency.minValue = 0.0;
        frequency.maxValue = 1000000.0;
        frequency.stepValue = 1.0;
        frequency.unit = QStringLiteral("Hz");

        TPSParamDefinition duty;
        duty.key = QStringLiteral("%1.duty").arg(identifier);
        duty.label = QStringLiteral("%1占空比").arg(labelPrefix);
        duty.type = TPSParamType::Double;
        duty.defaultValue = 0.5;
        duty.minValue = 0.0;
        duty.maxValue = 1.0;
        duty.stepValue = 0.01;

        requirement.parameters.push_back(waveform);
        requirement.parameters.push_back(amplitude);
        requirement.parameters.push_back(frequency);
        requirement.parameters.push_back(duty);
    };

    addPortParams(QStringLiteral("currentOut1"), QStringLiteral("电流输出1"), QStringLiteral("A"));
    addPortParams(QStringLiteral("voltageOut1"), QStringLiteral("电压输出1"), QStringLiteral("V"));
    addPortParams(QStringLiteral("voltageOut2"), QStringLiteral("电压输出2"), QStringLiteral("V"));

    auto addInputReference = [&](const QString &identifier, const QString &labelPrefix, const QString &unit) {
        TPSParamDefinition reference;
        reference.key = QStringLiteral("%1.reference").arg(identifier);
        reference.label = QStringLiteral("%1标准值").arg(labelPrefix);
        reference.type = TPSParamType::Double;
        reference.defaultValue = 0.0;
        reference.minValue = -1e9;
        reference.maxValue = 1e9;
        reference.stepValue = 0.01;
        reference.unit = unit;
        requirement.parameters.push_back(reference);
    };

    addInputReference(QStringLiteral("currentIn1"), QStringLiteral("电流采集1"), QStringLiteral("A"));
    addInputReference(QStringLiteral("voltageIn1"), QStringLiteral("电压采集1"), QStringLiteral("V"));
    addInputReference(QStringLiteral("voltageIn2"), QStringLiteral("电压采集2"), QStringLiteral("V"));

    return requirement;
}

bool MultiSignalTpsPlugin::buildDevicePlan(const QVector<TPSPortBinding> &bindings,
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

    TPSDevicePlan devicePlan;
    devicePlan.bindings = bindings;
    devicePlan.cfg532x = build532xInitConfig(JYDeviceKind::PXIe5322);
    devicePlan.cfg5711 = build5711InitConfig();
    devicePlan.cfg8902 = build8902InitConfig();

    int max5711Channel = -1;
    int currentInputCount = 0;
    int voltageInputCount = 0;
    int dmmCount = 0;

    for (const auto &binding : bindings) {
        switch (binding.type) {
            case TPSPortType::CurrentOutput:
            case TPSPortType::VoltageOutput:
                max5711Channel = qMax(max5711Channel, binding.channel);
                break;
            case TPSPortType::CurrentInput:
                ++currentInputCount;
                break;
            case TPSPortType::VoltageInput:
                ++voltageInputCount;
                break;
            case TPSPortType::DmmChannel:
                ++dmmCount;
                break;
        }
    }

    if (max5711Channel >= 0) {
        devicePlan.cfg5711.cfg5711.channelCount = max5711Channel + 1;
        devicePlan.cfg5711.cfg5711.enabledChannels.clear();
        devicePlan.cfg5711.cfg5711.waveforms.clear();
    } else {
        devicePlan.cfg5711.cfg5711.channelCount = 0;
        devicePlan.cfg5711.cfg5711.slotNumber = -1;
    }

    const int maxInputCount = qMax(currentInputCount, voltageInputCount);
    if (maxInputCount > 0) {
        devicePlan.cfg532x.cfg532x.channelCount = maxInputCount;
    } else {
        devicePlan.cfg532x.cfg532x.channelCount = 0;
        devicePlan.cfg532x.cfg532x.slotNumber = -1;
    }

    if (dmmCount > 0) {
        devicePlan.cfg8902.cfg8902.sampleCount = 20;
    } else {
        devicePlan.cfg8902.cfg8902.sampleCount = 0;
        devicePlan.cfg8902.cfg8902.slotNumber = -1;
    }

    for (const auto &binding : bindings) {
        TPSSignalRequest request;
        request.id = binding.identifier;
        request.signalType = binding.identifier;

        if (binding.type == TPSPortType::CurrentOutput || binding.type == TPSPortType::VoltageOutput) {
            const QString prefix = binding.identifier;
            const QString waveformKey = QStringLiteral("%1.waveform").arg(prefix);
            const QString ampKey = QStringLiteral("%1.amplitude").arg(prefix);
            const QString freqKey = QStringLiteral("%1.frequency").arg(prefix);
            const QString dutyKey = QStringLiteral("%1.duty").arg(prefix);

            const QString waveformName = settings.value(waveformKey, QStringLiteral("HighLevelWave")).toString();
            const double amplitude = settings.value(ampKey, 1.0).toDouble();
            const double frequency = settings.value(freqKey, 1000.0).toDouble();
            const double duty = settings.value(dutyKey, 0.5).toDouble();

            JY5711WaveformConfig config;
            config.channel = binding.channel;
            config.type = waveformFromString(waveformName);
            config.amplitude = amplitude;
            config.frequency = frequency;
            config.dutyCycle = duty;

            devicePlan.cfg5711.cfg5711.enabledChannels.push_back(binding.channel);
            devicePlan.cfg5711.cfg5711.waveforms.push_back(config);

            request.value = amplitude;
            request.unit = (binding.type == TPSPortType::CurrentOutput) ? QStringLiteral("A") : QStringLiteral("V");
        } else if (binding.type == TPSPortType::CurrentInput) {
            request.unit = QStringLiteral("A");
        } else if (binding.type == TPSPortType::VoltageInput) {
            request.unit = QStringLiteral("V");
        } else if (binding.type == TPSPortType::DmmChannel) {
            request.unit = QStringLiteral("ohm");
        }

        devicePlan.requests.push_back(request);
    }

    *plan = devicePlan;
    return true;
}

bool MultiSignalTpsPlugin::configure(const QMap<QString, QVariant> &settings, QString *error)
{
    Q_UNUSED(error)
    m_settings = settings;
    return true;
}

bool MultiSignalTpsPlugin::execute(const TPSRequest &request, TPSResult *result, QString *error)
{
    if (!result) {
        if (error) {
            *error = QStringLiteral("result is null");
        }
        return false;
    }

    result->runId = request.runId;
    result->success = true;
    result->summary = QStringLiteral("executed %1 items").arg(request.items.size());
    result->metrics.insert(QStringLiteral("items"), request.items.size());
    result->metrics.insert(QStringLiteral("settingsCount"), m_settings.size());
    return true;
}
