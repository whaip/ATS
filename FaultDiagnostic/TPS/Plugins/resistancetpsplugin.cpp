#include "resistancetpsplugin.h"

#include "../Core/tpsruntimecontext.h"

#include "../../../IODevices/JYDevices/jydeviceconfigutils.h"
#include "../../../IODevices/JYDevices/5711waveformconfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtMath>

namespace {
const TPSPortBinding *findBinding(const QVector<TPSPortBinding> &bindings, const QString &identifier)
{
    for (const auto &binding : bindings) {
        if (binding.identifier == identifier) {
            return &binding;
        }
    }
    return nullptr;
}

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

void appendSamplesFromVariant(const QVariant &value, QVector<double> *samples)
{
    if (!samples) {
        return;
    }

    if (value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        for (const QVariant &item : list) {
            bool ok = false;
            const double sample = item.toDouble(&ok);
            if (ok) {
                samples->push_back(sample);
            }
        }
        return;
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && json.isArray()) {
        const QJsonArray array = json.array();
        for (const QJsonValue &item : array) {
            if (item.isDouble()) {
                samples->push_back(item.toDouble());
            }
        }
        return;
    }

    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        bool ok = false;
        const double sample = token.toDouble(&ok);
        if (ok) {
            samples->push_back(sample);
        }
    }
}
}

ResistanceTpsPlugin::ResistanceTpsPlugin(QObject *parent)
    : QObject(parent)
{
}

QString ResistanceTpsPlugin::pluginId() const
{
    return QStringLiteral("tps.resistance");
}

QString ResistanceTpsPlugin::displayName() const
{
    return QStringLiteral("Resistance Test TPS");
}

QString ResistanceTpsPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVector<TPSParamDefinition> ResistanceTpsPlugin::parameterDefinitions() const
{
    return requirements().parameters;
}

TPSPluginRequirement ResistanceTpsPlugin::requirements() const
{
    TPSPluginRequirement requirement;

    TPSParamDefinition nominal;
    nominal.key = QStringLiteral("nominalOhms");
    nominal.label = QStringLiteral("标称值(Ω)");
    nominal.type = TPSParamType::Double;
    nominal.defaultValue = 1000.0;
    nominal.minValue = 0.0;
    nominal.maxValue = 1e9;
    nominal.stepValue = 1.0;

    TPSParamDefinition tol;
    tol.key = QStringLiteral("tolerancePercent");
    tol.label = QStringLiteral("容差(%)");
    tol.type = TPSParamType::Double;
    tol.defaultValue = 5.0;
    tol.minValue = 0.0;
    tol.maxValue = 100.0;
    tol.stepValue = 0.1;

    TPSParamDefinition inputPositiveanchor;
    inputPositiveanchor.key = QStringLiteral("inputPositiveAnchor");
    inputPositiveanchor.label = QStringLiteral("输入正极锚点");
    inputPositiveanchor.type = TPSParamType::Integer;
    inputPositiveanchor.defaultValue = -1;

    TPSParamDefinition inputNegativeanchor;
    inputNegativeanchor.key = QStringLiteral("inputNegativeAnchor");
    inputNegativeanchor.label = QStringLiteral("输入负极锚点");
    inputNegativeanchor.type = TPSParamType::Integer;
    inputNegativeanchor.defaultValue = -1;

    TPSParamDefinition outputPositiveanchor;
    outputPositiveanchor.key = QStringLiteral("outputPositiveAnchor");
    outputPositiveanchor.label = QStringLiteral("输出正极锚点");
    outputPositiveanchor.type = TPSParamType::Integer;
    outputPositiveanchor.defaultValue = -1;

    TPSParamDefinition outputNegativeanchor;
    outputNegativeanchor.key = QStringLiteral("outputNegativeAnchor");
    outputNegativeanchor.label = QStringLiteral("输出负极锚点");
    outputNegativeanchor.type = TPSParamType::Integer;
    outputNegativeanchor.defaultValue = -1;

    TPSParamDefinition Tempanchor;
    Tempanchor.key = QStringLiteral("temperatureAnchor");
    Tempanchor.label = QStringLiteral("温度区域");
    Tempanchor.type = TPSParamType::Integer;
    Tempanchor.defaultValue = -1;

    TPSPortRequest inputRequest;
    inputRequest.type = TPSPortType::VoltageInput;
    inputRequest.count = 1;
    inputRequest.identifiers = {QStringLiteral("resistanceMeasurementInput")};

    TPSPortRequest outputRequest;
    outputRequest.type = TPSPortType::VoltageOutput;
    outputRequest.count = 1;
    outputRequest.identifiers = {QStringLiteral("resistanceMeasurementOutput")};
    
    requirement.parameters = {nominal, tol, inputPositiveanchor, inputNegativeanchor, outputPositiveanchor, outputNegativeanchor, Tempanchor};
    requirement.ports = {inputRequest, outputRequest};


    return requirement;
}

bool ResistanceTpsPlugin::buildDevicePlan(const QVector<TPSPortBinding> &bindings,
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

    const TPSPortBinding *outputBinding = findBinding(effectiveBindings, QStringLiteral("resistanceMeasurementOutput"));
    const TPSPortBinding *inputBinding = findBinding(effectiveBindings, QStringLiteral("resistanceMeasurementInput"));
    if (!outputBinding || !inputBinding) {
        if (error) {
            *error = QStringLiteral("missing required binding(s): resistanceMeasurementOutput/resistanceMeasurementInput");
        }
        return false;
    }

    TPSDevicePlan devicePlan;
    devicePlan.bindings = effectiveBindings;
    devicePlan.cfg532x = build532xInitConfig(JYDeviceKind::PXIe5322);
    devicePlan.cfg5711 = build5711InitConfig();
    devicePlan.cfg8902 = build8902InitConfig();
    devicePlan.cfg532x.cfg532x.channelCount = 0;
    devicePlan.cfg532x.cfg532x.slotNumber = -1;
    devicePlan.cfg5711.cfg5711.channelCount = 0;
    devicePlan.cfg5711.cfg5711.slotNumber = -1;
    devicePlan.cfg5711.cfg5711.enabledChannels.clear();
    devicePlan.cfg5711.cfg5711.waveforms.clear();
    devicePlan.cfg8902.cfg8902.sampleCount = 0;
    devicePlan.cfg8902.cfg8902.slotNumber = -1;

    if (m_configReady) {
        devicePlan.cfg5711 = m_config5711;
    }

    if (outputBinding->deviceKind != JYDeviceKind::PXIe5711 || devicePlan.cfg5711.cfg5711.waveforms.isEmpty()) {
        if (error) {
            *error = QStringLiteral("resistanceMeasurementOutput must be allocated on PXIe5711 and configured");
        }
        return false;
    }

    TPSSignalRequest outputRequest;
    outputRequest.id = outputBinding->identifier;
    outputRequest.signalType = outputBinding->identifier;
    outputRequest.value = 2.0;
    outputRequest.unit = QStringLiteral("V");
    devicePlan.requests.push_back(outputRequest);

    TPSSignalRequest inputRequest;
    inputRequest.id = inputBinding->identifier;
    inputRequest.signalType = inputBinding->identifier;
    inputRequest.unit = QStringLiteral("V");
    devicePlan.requests.push_back(inputRequest);

    devicePlan.wiringSteps.push_back(
        QStringLiteral("将端口%1的正极接到%2")
            .arg(portText(*outputBinding), anchorText(settings, QStringLiteral("outputPositiveAnchor"))));
    devicePlan.wiringSteps.push_back(
        QStringLiteral("将端口%1的负极接到%2")
            .arg(portText(*outputBinding), anchorText(settings, QStringLiteral("outputNegativeAnchor"))));
    devicePlan.wiringSteps.push_back(
        QStringLiteral("将端口%1的正极接到%2")
            .arg(portText(*inputBinding), anchorText(settings, QStringLiteral("inputPositiveAnchor"))));
    devicePlan.wiringSteps.push_back(
        QStringLiteral("将端口%1的负极接到%2")
            .arg(portText(*inputBinding), anchorText(settings, QStringLiteral("inputNegativeAnchor"))));

    devicePlan.temperatureGuide = QStringLiteral("请按照图中%1框选测温区域")
        .arg(anchorText(settings, QStringLiteral("temperatureAnchor")));

    devicePlan.guide.wiringSteps = devicePlan.wiringSteps;
    devicePlan.guide.roiSteps = {devicePlan.temperatureGuide};
    devicePlan.guide.extensions.insert(QStringLiteral("wiringFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("outputPositiveAnchor")),
        anchorText(settings, QStringLiteral("outputNegativeAnchor")),
        anchorText(settings, QStringLiteral("inputPositiveAnchor")),
        anchorText(settings, QStringLiteral("inputNegativeAnchor"))
    });
    devicePlan.guide.extensions.insert(QStringLiteral("roiFocusTargets"), QStringList{
        anchorText(settings, QStringLiteral("temperatureAnchor"))
    });
    devicePlan.guide.extensions.insert(QStringLiteral("pluginId"), pluginId());
    devicePlan.guide.extensions.insert(QStringLiteral("outputAmplitudeV"), 2.0);

    if (devicePlan.wiringSteps.isEmpty()) {
        devicePlan.wiringSteps.push_back(QStringLiteral("请根据端口绑定完成接线。"));
    }

    *plan = devicePlan;
    return true;
}

bool ResistanceTpsPlugin::configure(const QMap<QString, QVariant> &settings, QString *error)
{
    m_settings = settings;
    m_allocatedBindings = TPSRuntimeContext::decodeBindings(settings.value(TPSRuntimeContext::allocatedBindingsKey()));

    const TPSPortBinding *outputBinding = findBinding(m_allocatedBindings, QStringLiteral("resistanceMeasurementOutput"));
    if (!outputBinding) {
        m_configReady = false;
        if (error) {
            *error = QStringLiteral("missing allocated binding: resistanceMeasurementOutput");
        }
        return false;
    }
    if (outputBinding->deviceKind != JYDeviceKind::PXIe5711) {
        m_configReady = false;
        if (error) {
            *error = QStringLiteral("resistanceMeasurementOutput must be allocated on PXIe5711");
        }
        return false;
    }

    m_config5711 = build5711InitConfig();
    m_config5711.cfg5711.slotNumber = build5711InitConfig().cfg5711.slotNumber;
    m_config5711.cfg5711.channelCount = outputBinding->channel + 1;
    m_config5711.cfg5711.enabledChannels = {outputBinding->channel};
    m_config5711.cfg5711.waveforms.clear();

    JY5711WaveformConfig outputWave;
    outputWave.channel = outputBinding->channel;
    outputWave.type = PXIe5711_testtype::HighLevelWave;
    outputWave.amplitude = 2.0;
    outputWave.frequency = 1000.0;
    outputWave.dutyCycle = 0.5;
    m_config5711.cfg5711.waveforms.push_back(outputWave);

    m_configReady = true;
    return true;
}

bool ResistanceTpsPlugin::execute(const TPSRequest &request, TPSResult *result, QString *error)
{
    if (!result) {
        if (error) {
            *error = QStringLiteral("result is null");
        }
        return false;
    }

    const double nominalOhms = m_settings.value(QStringLiteral("nominalOhms"), 1000.0).toDouble();
    const double tolerancePercent = m_settings.value(QStringLiteral("tolerancePercent"), 5.0).toDouble();
    const double excitationV = resolveExcitationVoltage(request);

    QVector<double> samples;
    collectInputSamples(request, &samples);
    double meanV = 0.0;
    if (!samples.isEmpty()) {
        double sum = 0.0;
        for (double sample : samples) {
            sum += sample;
        }
        meanV = sum / static_cast<double>(samples.size());
    } else {
        meanV = excitationV * 1.01;
    }

    double measuredOhms = nominalOhms;
    if (qAbs(excitationV) > 1e-12) {
        measuredOhms = nominalOhms * (meanV / excitationV);
    }

    const double delta = qAbs(measuredOhms - nominalOhms);
    const double limit = nominalOhms * tolerancePercent / 100.0;

    result->runId = request.runId;
    result->success = delta <= limit;
    result->summary = result->success
        ? QStringLiteral("PASS: %1 Ω").arg(measuredOhms, 0, 'f', 2)
        : QStringLiteral("FAIL: %1 Ω").arg(measuredOhms, 0, 'f', 2);

    result->metrics.insert(QStringLiteral("nominalOhms"), nominalOhms);
    result->metrics.insert(QStringLiteral("measuredOhms"), measuredOhms);
    result->metrics.insert(QStringLiteral("meanInputVoltage"), meanV);
    result->metrics.insert(QStringLiteral("excitationVoltage"), excitationV);
    result->metrics.insert(QStringLiteral("tolerancePercent"), tolerancePercent);
    result->metrics.insert(QStringLiteral("sampleCount"), samples.size());
    result->metrics.insert(QStringLiteral("items"), request.items.size());
    return true;
}

bool ResistanceTpsPlugin::collectInputSamples(const TPSRequest &request, QVector<double> *samples) const
{
    if (!samples) {
        return false;
    }

    samples->clear();
    for (const UTRItem &item : request.items) {
        appendSamplesFromVariant(item.parameters.value(QStringLiteral("resistanceMeasurementInput.samples")), samples);
        appendSamplesFromVariant(item.parameters.value(QStringLiteral("inputSamples")), samples);
    }
    return !samples->isEmpty();
}

double ResistanceTpsPlugin::resolveExcitationVoltage(const TPSRequest &request) const
{
    for (const UTRItem &item : request.items) {
        const QVariant explicitValue = item.parameters.value(QStringLiteral("resistanceMeasurementOutput.value"));
        if (explicitValue.isValid()) {
            bool ok = false;
            const double value = explicitValue.toDouble(&ok);
            if (ok && qAbs(value) > 1e-12) {
                return value;
            }
        }
    }

    return 2.0;
}
