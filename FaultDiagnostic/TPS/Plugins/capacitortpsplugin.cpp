#include "capacitortpsplugin.h"

#include "../Core/tpsruntimecontext.h"

#include "../../../IODevices/JYDevices/5711waveformconfig.h"
#include "../../../IODevices/JYDevices/jydeviceconfigutils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtMath>

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

double meanValue(const QVector<double> &data)
{
    if (data.isEmpty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (double v : data) {
        sum += v;
    }
    return sum / static_cast<double>(data.size());
}

double maxValue(const QVector<double> &data)
{
    if (data.isEmpty()) {
        return 0.0;
    }
    double m = data.first();
    for (double v : data) {
        if (v > m) {
            m = v;
        }
    }
    return m;
}

double minValue(const QVector<double> &data)
{
    if (data.isEmpty()) {
        return 0.0;
    }
    double m = data.first();
    for (double v : data) {
        if (v < m) {
            m = v;
        }
    }
    return m;
}

int findFirstCrossing(const QVector<double> &data, int startIndex, double threshold)
{
    for (int i = qMax(0, startIndex); i < data.size(); ++i) {
        if (data.at(i) >= threshold) {
            return i;
        }
    }
    return -1;
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

    TPSParamDefinition expectedMode;
    expectedMode.key = QStringLiteral("expectedMode");
    expectedMode.label = QStringLiteral("工况模式(MODE0~MODE4)");
    expectedMode.type = TPSParamType::Integer;
    expectedMode.defaultValue = 0;
    expectedMode.minValue = 0;
    expectedMode.maxValue = 4;
    expectedMode.stepValue = 1;

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
    outFrequency.maxValue = 1e6;
    outFrequency.stepValue = 0.1;

    TPSParamDefinition inputVcapAnchor;
    inputVcapAnchor.key = QStringLiteral("vcapAnchor");
    inputVcapAnchor.label = QStringLiteral("Vcap测点锚点");
    inputVcapAnchor.type = TPSParamType::Integer;
    inputVcapAnchor.defaultValue = -1;

    TPSParamDefinition inputVinAnchor;
    inputVinAnchor.key = QStringLiteral("vinAnchor");
    inputVinAnchor.label = QStringLiteral("Vin测点锚点");
    inputVinAnchor.type = TPSParamType::Integer;
    inputVinAnchor.defaultValue = -1;

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

    TPSPortRequest outputReq;
    outputReq.type = TPSPortType::VoltageOutput;
    outputReq.count = 1;
    outputReq.identifiers = {QStringLiteral("capacitorStimulusOutput")};

    TPSPortRequest inputReq;
    inputReq.type = TPSPortType::VoltageInput;
    inputReq.count = 2;
    inputReq.identifiers = {QStringLiteral("capVoltageInput"), QStringLiteral("vinVoltageInput")};

    requirement.parameters = {
        expectedMode,
        cNominal,
        rSeries,
        sampleRate,
        captureMs,
        modeTolerance,
        outAmplitude,
        outFrequency,
        inputVcapAnchor,
        inputVinAnchor,
        outputVinAnchor,
        outputGroundAnchor,
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

    JY5711WaveformConfig wave;
    wave.channel = output->channel;
    wave.type = PXIe5711_testtype::HighLevelWave;
    wave.amplitude = amplitude;
    wave.frequency = frequency;
    wave.dutyCycle = 0.5;
    devicePlan.cfg5711.cfg5711.waveforms.push_back(wave);

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
        QStringLiteral("将%1测量端接到%2").arg(portText(*vcapInput), anchorText(settings, QStringLiteral("vcapAnchor"))),
        QStringLiteral("将%1测量端接到%2").arg(portText(*vinInput), anchorText(settings, QStringLiteral("vinAnchor")))
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
        anchorText(settings, QStringLiteral("vcapAnchor")),
        anchorText(settings, QStringLiteral("vinAnchor"))
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

    JY5711WaveformConfig wave;
    wave.channel = output->channel;
    wave.type = PXIe5711_testtype::HighLevelWave;
    wave.amplitude = amplitude;
    wave.frequency = frequency;
    wave.dutyCycle = 0.5;
    m_config5711.cfg5711.waveforms.push_back(wave);

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

    SignalSeriesPair series;
    const bool gotSeries = collectSignalSeries(request, &series);
    FeatureSet features = estimateFeatures(series);

    if (!gotSeries) {
        const int fallbackMode = expectedMode();
        result->runId = request.runId;
        result->success = true;
        result->summary = QStringLiteral("%1 (fallback, no waveform samples)").arg(modeName(fallbackMode));
        result->metrics.insert(QStringLiteral("mode.predicted"), fallbackMode);
        result->metrics.insert(QStringLiteral("mode.expected"), fallbackMode);
        result->metrics.insert(QStringLiteral("sampleCount"), 0);
        return true;
    }

    const int predicted = classifyMode(features);
    const int expected = expectedMode();

    result->runId = request.runId;
    result->success = (predicted == expected);
    result->summary = QStringLiteral("Pred=%1, Exp=%2, tau=%3 ms, gain=%4")
        .arg(modeName(predicted))
        .arg(modeName(expected))
        .arg(features.estimatedTauSec * 1000.0, 0, 'f', 2)
        .arg(features.gain, 0, 'f', 3);

    result->metrics.insert(QStringLiteral("mode.predicted"), predicted);
    result->metrics.insert(QStringLiteral("mode.expected"), expected);
    result->metrics.insert(QStringLiteral("vinAmplitude"), features.vinAmplitude);
    result->metrics.insert(QStringLiteral("vcapAmplitude"), features.vcapAmplitude);
    result->metrics.insert(QStringLiteral("gain"), features.gain);
    result->metrics.insert(QStringLiteral("highRetention"), features.highRetention);
    result->metrics.insert(QStringLiteral("edgeDrop"), features.edgeDrop);
    result->metrics.insert(QStringLiteral("tau.estimatedSec"), features.estimatedTauSec);
    result->metrics.insert(QStringLiteral("tau.nominalSec"), features.nominalTauSec);
    result->metrics.insert(QStringLiteral("sampleCount"), features.sampleCount);

    return true;
}

bool CapacitorTpsPlugin::collectSignalSeries(const TPSRequest &request, SignalSeriesPair *series) const
{
    if (!series) {
        return false;
    }

    series->vin.clear();
    series->vcap.clear();
    series->sampleRateHz = m_settings.value(QStringLiteral("sampleRateHz"), 10000.0).toDouble();

    for (const UTRItem &item : request.items) {
        appendSamplesFromVariant(item.parameters.value(QStringLiteral("vinVoltageInput.samples")), &series->vin);
        appendSamplesFromVariant(item.parameters.value(QStringLiteral("capVoltageInput.samples")), &series->vcap);

        appendSamplesFromVariant(item.parameters.value(QStringLiteral("vinSamples")), &series->vin);
        appendSamplesFromVariant(item.parameters.value(QStringLiteral("vcapSamples")), &series->vcap);

        appendSamplesFromVariant(item.parameters.value(QStringLiteral("capVin.samples")), &series->vin);
        appendSamplesFromVariant(item.parameters.value(QStringLiteral("capVcap.samples")), &series->vcap);

        if (item.parameters.contains(QStringLiteral("sampleRateHz"))) {
            bool ok = false;
            const double fs = item.parameters.value(QStringLiteral("sampleRateHz")).toDouble(&ok);
            if (ok && fs > 0.0) {
                series->sampleRateHz = fs;
            }
        }
    }

    const int count = qMin(series->vin.size(), series->vcap.size());
    if (count <= 8) {
        return false;
    }

    series->vin.resize(count);
    series->vcap.resize(count);
    if (series->sampleRateHz <= 0.0) {
        series->sampleRateHz = m_settings.value(QStringLiteral("sampleRateHz"), 10000.0).toDouble();
    }
    return series->sampleRateHz > 0.0;
}

CapacitorTpsPlugin::FeatureSet CapacitorTpsPlugin::estimateFeatures(const SignalSeriesPair &series) const
{
    FeatureSet f;
    f.sampleCount = qMin(series.vin.size(), series.vcap.size());
    if (f.sampleCount <= 8) {
        return f;
    }

    const QVector<double> vin = series.vin;
    const QVector<double> vcap = series.vcap;

    const double vinMax = maxValue(vin);
    const double vinMin = minValue(vin);
    const double vcapMax = maxValue(vcap);
    const double vcapMin = minValue(vcap);

    f.vinAmplitude = vinMax - vinMin;
    f.vcapAmplitude = vcapMax - vcapMin;
    f.gain = (qAbs(f.vinAmplitude) > 1e-12) ? (f.vcapAmplitude / f.vinAmplitude) : 0.0;

    const int tailStart = qMax(0, f.sampleCount * 3 / 4);
    QVector<double> vinTail;
    QVector<double> vcapTail;
    vinTail.reserve(f.sampleCount - tailStart);
    vcapTail.reserve(f.sampleCount - tailStart);
    for (int i = tailStart; i < f.sampleCount; ++i) {
        vinTail.push_back(vin.at(i));
        vcapTail.push_back(vcap.at(i));
    }
    const double vinHigh = meanValue(vinTail);
    const double vcapHigh = meanValue(vcapTail);
    f.highRetention = (qAbs(vinHigh) > 1e-12) ? (vcapHigh / vinHigh) : 0.0;

    const int edgeWindow = qMin(10, f.sampleCount - 1);
    double dropSum = 0.0;
    for (int i = 0; i < edgeWindow; ++i) {
        dropSum += qAbs(vin.at(i) - vcap.at(i));
    }
    f.edgeDrop = dropSum / static_cast<double>(edgeWindow);

    const double vinThreshold = vinMin + 0.5 * (vinMax - vinMin);
    int riseIndex = -1;
    for (int i = 1; i < f.sampleCount; ++i) {
        if (vin.at(i - 1) < vinThreshold && vin.at(i) >= vinThreshold) {
            riseIndex = i;
            break;
        }
    }
    if (riseIndex < 0) {
        riseIndex = 0;
    }

    const int settleEnd = qMin(f.sampleCount - 1, riseIndex + qMax(20, f.sampleCount / 3));
    QVector<double> settleSegment;
    settleSegment.reserve(settleEnd - riseIndex + 1);
    for (int i = riseIndex; i <= settleEnd; ++i) {
        settleSegment.push_back(vcap.at(i));
    }

    const double startV = vcap.value(riseIndex);
    const double finalV = meanValue(settleSegment);
    const double targetV = startV + 0.632 * (finalV - startV);
    const int tauIndex = findFirstCrossing(vcap, riseIndex, targetV);

    if (tauIndex > riseIndex && series.sampleRateHz > 0.0) {
        f.estimatedTauSec = static_cast<double>(tauIndex - riseIndex) / series.sampleRateHz;
    }

    const double cNominalF = m_settings.value(QStringLiteral("cNominal_uF"), 10.0).toDouble() * 1e-6;
    const double r1 = m_settings.value(QStringLiteral("r1Ohms"), 100.0).toDouble();
    f.nominalTauSec = qMax(0.0, cNominalF * r1);

    return f;
}

int CapacitorTpsPlugin::classifyMode(const FeatureSet &f) const
{
    const double tauTol = m_settings.value(QStringLiteral("tauDriftTolerance"), 0.35).toDouble();
    const double shortGainThreshold = 0.10;
    const double openGainThreshold = 0.92;
    const double openTauFactor = 0.30;
    const double leakRetentionThreshold = 0.85;
    const double esrDropThreshold = 0.12 * qMax(0.1, m_settings.value(QStringLiteral("stimulusAmplitudeV"), 2.0).toDouble());

    if (f.gain <= shortGainThreshold || f.vcapAmplitude < 0.05 * qMax(0.1, f.vinAmplitude)) {
        return 3;
    }

    if ((f.highRetention > 0.0 && f.highRetention < leakRetentionThreshold)
        || f.edgeDrop >= esrDropThreshold) {
        return 4;
    }

    if (f.nominalTauSec > 0.0 && f.estimatedTauSec > 0.0
        && f.gain >= openGainThreshold
        && f.estimatedTauSec < f.nominalTauSec * openTauFactor) {
        return 2;
    }

    if (f.nominalTauSec > 0.0 && f.estimatedTauSec > 0.0) {
        const double ratio = f.estimatedTauSec / f.nominalTauSec;
        if (qAbs(ratio - 1.0) > tauTol) {
            return 1;
        }
    }

    return 0;
}

int CapacitorTpsPlugin::expectedMode() const
{
    return resolveModeValue(m_settings.value(QStringLiteral("expectedMode")), 0);
}

void CapacitorTpsPlugin::appendSamplesFromVariant(const QVariant &value, QVector<double> *samples)
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

QString CapacitorTpsPlugin::modeName(int mode)
{
    switch (mode) {
    case 0:
        return QStringLiteral("MODE0(Normal)");
    case 1:
        return QStringLiteral("MODE1(C_drift)");
    case 2:
        return QStringLiteral("MODE2(Open)");
    case 3:
        return QStringLiteral("MODE3(Short)");
    case 4:
        return QStringLiteral("MODE4(Leak_ESR)");
    default:
        break;
    }
    return QStringLiteral("MODE?(Unknown)");
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
