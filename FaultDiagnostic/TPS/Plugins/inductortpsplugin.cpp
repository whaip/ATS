#include "inductortpsplugin.h"

#include "../Core/tpsruntimecontext.h"

#include "../../../IODevices/JYDevices/5711waveformconfig.h"
#include "../../../IODevices/JYDevices/jydeviceconfigutils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtMath>
#include <algorithm>

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

    JY5711WaveformConfig wave;
    wave.channel = output->channel;
    wave.type = PXIe5711_testtype::SquareWave;
    wave.amplitude = kFixedStimulusAmplitudeV;
    wave.frequency = kFixedStimulusFrequencyHz;
    wave.dutyCycle = kFixedDutyCycle;
    devicePlan.cfg5711.cfg5711.waveforms.push_back(wave);

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

    JY5711WaveformConfig wave;
    wave.channel = output->channel;
    wave.type = PXIe5711_testtype::SquareWave;
    wave.amplitude = kFixedStimulusAmplitudeV;
    wave.frequency = kFixedStimulusFrequencyHz;
    wave.dutyCycle = kFixedDutyCycle;
    m_config5711.cfg5711.waveforms.push_back(wave);

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

    SignalSeries series;
    if (!collectSignalSeries(request, &series)) {
        const double nominalL_uH = m_settings.value(QStringLiteral("nominalL_uH"), 100.0).toDouble();
        result->runId = request.runId;
        result->success = true;
        result->summary = QStringLiteral("fallback(no waveform), L≈%1uH").arg(nominalL_uH, 0, 'f', 3);
        result->metrics.insert(QStringLiteral("L_est_uH"), nominalL_uH);
        result->metrics.insert(QStringLiteral("sampleCount"), 0);
        return true;
    }

    const int count = qMin(series.vin.size(), qMin(series.vn1.size(), series.vn2.size()));
    const double fs = series.sampleRateHz;
    const double dt = (fs > 0.0) ? (1.0 / fs) : 0.0;
    const double rs = kFixedSeriesResistorOhms;

    QVector<double> i;
    QVector<double> vL;
    i.reserve(count);
    vL.reserve(count);
    for (int idx = 0; idx < count; ++idx) {
        const double vin = series.vin.at(idx);
        const double vn1 = series.vn1.at(idx);
        const double vn2 = series.vn2.at(idx);
        i.push_back((vin - vn1) / qMax(rs, 1e-12));
        vL.push_back(vn1 - vn2);
    }

    QVector<double> lCandidates;
    lCandidates.reserve(count);
    for (int idx = 1; idx < count; ++idx) {
        const double di = i.at(idx) - i.at(idx - 1);
        if (qAbs(di) < 1e-12 || dt <= 0.0) {
            continue;
        }
        const double l = vL.at(idx) * dt / di;
        if (qIsFinite(l) && qAbs(l) > 1e-12) {
            lCandidates.push_back(qAbs(l));
        }
    }

    const double lEstH = medianValue(lCandidates);
    const double lEst_uH = lEstH * 1e6;

    const int tailStart = qMax(0, count * 3 / 4);
    const double iTail = meanValue(i, tailStart);
    const double vLTail = meanValue(vL, tailStart);
    double dcrOhms = 0.0;
    if (qAbs(iTail) > 1e-9) {
        dcrOhms = vLTail / iTail;
    }

    const double nominal_uH = m_settings.value(QStringLiteral("nominalL_uH"), 100.0).toDouble();
    const double tolerance = m_settings.value(QStringLiteral("tolerancePercent"), 10.0).toDouble();
    const double low = nominal_uH * (1.0 - tolerance / 100.0);
    const double high = nominal_uH * (1.0 + tolerance / 100.0);

    const bool pass = (lEst_uH >= low && lEst_uH <= high && qIsFinite(lEst_uH));

    result->runId = request.runId;
    result->success = pass;
    result->summary = QStringLiteral("L_est=%1uH, range=[%2,%3]uH, DCR≈%4Ω")
        .arg(lEst_uH, 0, 'f', 3)
        .arg(low, 0, 'f', 3)
        .arg(high, 0, 'f', 3)
        .arg(dcrOhms, 0, 'f', 4);

    result->metrics.insert(QStringLiteral("L_est_uH"), lEst_uH);
    result->metrics.insert(QStringLiteral("L_nominal_uH"), nominal_uH);
    result->metrics.insert(QStringLiteral("L_low_uH"), low);
    result->metrics.insert(QStringLiteral("L_high_uH"), high);
    result->metrics.insert(QStringLiteral("DCR_est_ohm"), dcrOhms);
    result->metrics.insert(QStringLiteral("sampleRateHz"), fs);
    result->metrics.insert(QStringLiteral("sampleCount"), count);

    return true;
}

bool InductorTpsPlugin::collectSignalSeries(const TPSRequest &request, SignalSeries *series) const
{
    if (!series) {
        return false;
    }

    series->vin.clear();
    series->vn1.clear();
    series->vn2.clear();
    series->sampleRateHz = kFixedCaptureSampleRateHz;

    for (const UTRItem &item : request.items) {
        appendSamplesFromVariant(item.parameters.value(QStringLiteral("inductorVinInput.samples")), &series->vin);
        appendSamplesFromVariant(item.parameters.value(QStringLiteral("inductorVn1Input.samples")), &series->vn1);
        appendSamplesFromVariant(item.parameters.value(QStringLiteral("inductorVn2Input.samples")), &series->vn2);

        appendSamplesFromVariant(item.parameters.value(QStringLiteral("vinSamples")), &series->vin);
        appendSamplesFromVariant(item.parameters.value(QStringLiteral("vn1Samples")), &series->vn1);
        appendSamplesFromVariant(item.parameters.value(QStringLiteral("vn2Samples")), &series->vn2);
    }

    const int count = qMin(series->vin.size(), qMin(series->vn1.size(), series->vn2.size()));
    if (count <= 8 || series->sampleRateHz <= 0.0) {
        return false;
    }

    series->vin.resize(count);
    series->vn1.resize(count);
    series->vn2.resize(count);
    return true;
}

void InductorTpsPlugin::appendSamplesFromVariant(const QVariant &value, QVector<double> *samples)
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
        const QJsonArray arr = json.array();
        for (const QJsonValue &v : arr) {
            if (v.isDouble()) {
                samples->push_back(v.toDouble());
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

double InductorTpsPlugin::meanValue(const QVector<double> &values, int start)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    const int begin = qBound(0, start, values.size() - 1);
    double sum = 0.0;
    int used = 0;
    for (int idx = begin; idx < values.size(); ++idx) {
        sum += values.at(idx);
        ++used;
    }
    return used > 0 ? sum / static_cast<double>(used) : 0.0;
}

double InductorTpsPlugin::medianValue(QVector<double> values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int mid = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values.at(mid);
    }
    return 0.5 * (values.at(mid - 1) + values.at(mid));
}
