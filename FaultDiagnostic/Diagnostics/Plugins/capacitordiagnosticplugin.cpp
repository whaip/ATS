#include "capacitordiagnosticplugin.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

double clampMin(double value, double minValue)
{
    return value < minValue ? minValue : value;
}

double meanRange(const QVector<double> &values, int begin, int end)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    const int left = qBound(0, begin, values.size());
    const int right = qBound(left, end, values.size());
    if (left >= right) {
        return values.value(left, 0.0);
    }
    double sum = 0.0;
    for (int index = left; index < right; ++index) {
        sum += values.at(index);
    }
    return sum / static_cast<double>(right - left);
}

double meanAll(const QVector<double> &values)
{
    return meanRange(values, 0, values.size());
}

double maxValue(const QVector<double> &values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    double result = values.first();
    for (double sample : values) {
        if (sample > result) {
            result = sample;
        }
    }
    return result;
}

double minValue(const QVector<double> &values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    double result = values.first();
    for (double sample : values) {
        if (sample < result) {
            result = sample;
        }
    }
    return result;
}

double medianValue(QVector<double> values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const int middle = values.size() / 2;
    if ((values.size() % 2) == 1) {
        return values.at(middle);
    }
    return 0.5 * (values.at(middle - 1) + values.at(middle));
}

double pearsonCorrelation(const QVector<double> &a, const QVector<double> &b)
{
    const int count = qMin(a.size(), b.size());
    if (count <= 1) {
        return 0.0;
    }

    const double meanA = meanRange(a, 0, count);
    const double meanB = meanRange(b, 0, count);

    double numerator = 0.0;
    double varianceA = 0.0;
    double varianceB = 0.0;
    for (int index = 0; index < count; ++index) {
        const double da = a.at(index) - meanA;
        const double db = b.at(index) - meanB;
        numerator += da * db;
        varianceA += da * da;
        varianceB += db * db;
    }

    const double denominator = std::sqrt(varianceA * varianceB);
    if (denominator <= 1e-18) {
        return 0.0;
    }
    return numerator / denominator;
}

int parseExpectedMode(const QVariant &value, int fallback)
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

QString modeName(int mode)
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
    }
    return QStringLiteral("MODE?(Unknown)");
}

int firstCrossingAbove(const QVector<double> &values, int begin, int end, double threshold)
{
    const int left = qBound(0, begin, values.size());
    const int right = qBound(left, end, values.size());
    for (int index = left; index < right; ++index) {
        if (values.at(index) >= threshold) {
            return index;
        }
    }
    return -1;
}

int firstCrossingBelow(const QVector<double> &values, int begin, int end, double threshold)
{
    const int left = qBound(0, begin, values.size());
    const int right = qBound(left, end, values.size());
    for (int index = left; index < right; ++index) {
        if (values.at(index) <= threshold) {
            return index;
        }
    }
    return -1;
}

struct CycleFeature {
    int riseIndex = -1;
    int fallIndex = -1;
    int nextRiseIndex = -1;
    double highPlateau = 0.0;
    double lowPlateau = 0.0;
    double amplitude = 0.0;
    double tauRiseSec = 0.0;
    double tauDischargeSec = 0.0;
    double edgeDrop = 0.0;
};

struct CapacitorFeatures {
    int sampleCount = 0;
    int cycleCount = 0;
    double sampleRateHz = 0.0;
    double vinAmplitude = 0.0;
    double vcapMax = 0.0;
    double vcapMin = 0.0;
    double vcapAmplitude = 0.0;
    double highRetention = 0.0;
    double followCorrelation = 0.0;
    double followNmae = 0.0;
    double tauRiseSec = 0.0;
    double tauDischargeSec = 0.0;
    double nominalTauSec = 0.0;
    double edgeDrop = 0.0;
    double tempMaxC = std::numeric_limits<double>::quiet_NaN();
};

QVector<int> detectRisingEdges(const QVector<double> &vin)
{
    QVector<int> rising;
    if (vin.size() < 3) {
        return rising;
    }

    const double low = minValue(vin);
    const double high = maxValue(vin);
    const double threshold = low + 0.5 * (high - low);
    for (int index = 1; index < vin.size(); ++index) {
        if (vin.at(index - 1) < threshold && vin.at(index) >= threshold) {
            rising.push_back(index);
        }
    }
    return rising;
}

QVector<int> detectFallingEdges(const QVector<double> &vin)
{
    QVector<int> falling;
    if (vin.size() < 3) {
        return falling;
    }

    const double low = minValue(vin);
    const double high = maxValue(vin);
    const double threshold = low + 0.5 * (high - low);
    for (int index = 1; index < vin.size(); ++index) {
        if (vin.at(index - 1) > threshold && vin.at(index) <= threshold) {
            falling.push_back(index);
        }
    }
    return falling;
}

double firstPresent(const QMap<QString, QVariant> &parameters,
                    const QStringList &keys,
                    double defaultValue,
                    bool *present = nullptr)
{
    for (const QString &key : keys) {
        if (parameters.contains(key)) {
            if (present) {
                *present = true;
            }
            return parameters.value(key).toDouble();
        }
    }
    if (present) {
        *present = false;
    }
    return defaultValue;
}

CapacitorFeatures extractFeatures(const DiagnosticInput &input)
{
    CapacitorFeatures features;

    const DiagnosticSignalSeries vcapSeries = input.signalSeries.value(QStringLiteral("capVoltageInput"));
    const DiagnosticSignalSeries vinSeries = input.signalSeries.value(QStringLiteral("vinVoltageInput"));

    QVector<double> vcap = vcapSeries.samples;
    QVector<double> vin = vinSeries.samples;
    const int count = qMin(vcap.size(), vin.size());
    if (count <= 8) {
        return features;
    }
    vcap.resize(count);
    vin.resize(count);

    features.sampleCount = count;
    features.sampleRateHz = vcapSeries.sampleRateHz > 0.0
        ? vcapSeries.sampleRateHz
        : (vinSeries.sampleRateHz > 0.0 ? vinSeries.sampleRateHz : input.parameters.value(QStringLiteral("sampleRateHz"), 1e6).toDouble());

    features.vcapMax = maxValue(vcap);
    features.vcapMin = minValue(vcap);
    features.vcapAmplitude = features.vcapMax - features.vcapMin;
    const double vinMax = maxValue(vin);
    const double vinMin = minValue(vin);
    features.vinAmplitude = vinMax - vinMin;

    const double vinAmpSafe = clampMin(features.vinAmplitude, 1e-9);
    double absDiffSum = 0.0;
    for (int index = 0; index < count; ++index) {
        absDiffSum += std::fabs(vcap.at(index) - vin.at(index));
    }
    features.followNmae = (absDiffSum / static_cast<double>(count)) / vinAmpSafe;
    features.followCorrelation = pearsonCorrelation(vcap, vin);

    const QVector<int> rising = detectRisingEdges(vin);
    const QVector<int> falling = detectFallingEdges(vin);

    QVector<CycleFeature> cycles;
    for (int cycleIndex = 0; cycleIndex + 1 < rising.size(); ++cycleIndex) {
        const int riseIndex = rising.at(cycleIndex);
        const int nextRise = rising.at(cycleIndex + 1);
        if (nextRise <= riseIndex + 4) {
            continue;
        }

        int fallIndex = -1;
        for (int edge : falling) {
            if (edge > riseIndex && edge < nextRise) {
                fallIndex = edge;
                break;
            }
        }
        if (fallIndex <= riseIndex + 2 || fallIndex >= nextRise - 2) {
            continue;
        }

        const int ton = fallIndex - riseIndex;
        const int toff = nextRise - fallIndex;
        if (ton < 4 || toff < 4) {
            continue;
        }

        CycleFeature cycle;
        cycle.riseIndex = riseIndex;
        cycle.fallIndex = fallIndex;
        cycle.nextRiseIndex = nextRise;

        const int highBegin = riseIndex + static_cast<int>(std::round(ton * 0.6));
        const int highEnd = riseIndex + static_cast<int>(std::round(ton * 0.9));
        cycle.highPlateau = meanRange(vcap, highBegin, highEnd);

        const int lowBegin = fallIndex + static_cast<int>(std::round(toff * 0.2));
        const int lowEnd = fallIndex + static_cast<int>(std::round(toff * 0.8));
        cycle.lowPlateau = meanRange(vcap, lowBegin, lowEnd);

        cycle.amplitude = cycle.highPlateau - cycle.lowPlateau;

        const int baseBegin = qMax(0, riseIndex - qMax(3, ton / 5));
        const double baseValue = meanRange(vcap, baseBegin, riseIndex);
        const double riseTarget = baseValue + 0.632 * (cycle.highPlateau - baseValue);
        const int reach63 = firstCrossingAbove(vcap, riseIndex, fallIndex, riseTarget);
        if (reach63 > riseIndex && features.sampleRateHz > 0.0) {
            cycle.tauRiseSec = static_cast<double>(reach63 - riseIndex) / features.sampleRateHz;
        }

        const double dischargeTarget = cycle.lowPlateau + 0.368 * (cycle.highPlateau - cycle.lowPlateau);
        const int reachDis = firstCrossingBelow(vcap, fallIndex, nextRise, dischargeTarget);
        if (reachDis > fallIndex && features.sampleRateHz > 0.0) {
            cycle.tauDischargeSec = static_cast<double>(reachDis - fallIndex) / features.sampleRateHz;
        }

        const int edgeProbe = qMin(fallIndex - 1, riseIndex + qMax(1, ton / 20));
        if (edgeProbe > riseIndex) {
            const double early = meanRange(vcap, riseIndex, edgeProbe + 1);
            cycle.edgeDrop = qMax(0.0, cycle.highPlateau - early);
        }

        cycles.push_back(cycle);
    }

    features.cycleCount = cycles.size();

    QVector<double> highPlateaus;
    QVector<double> lowPlateaus;
    QVector<double> amplitudes;
    QVector<double> tauRise;
    QVector<double> tauDis;
    QVector<double> edgeDrops;

    for (const CycleFeature &cycle : cycles) {
        highPlateaus.push_back(cycle.highPlateau);
        lowPlateaus.push_back(cycle.lowPlateau);
        amplitudes.push_back(cycle.amplitude);
        if (cycle.tauRiseSec > 0.0) {
            tauRise.push_back(cycle.tauRiseSec);
        }
        if (cycle.tauDischargeSec > 0.0) {
            tauDis.push_back(cycle.tauDischargeSec);
        }
        edgeDrops.push_back(cycle.edgeDrop);
    }

    const double highMedian = highPlateaus.isEmpty() ? meanAll(vcap) : medianValue(highPlateaus);
    const double lowMedian = lowPlateaus.isEmpty() ? features.vcapMin : medianValue(lowPlateaus);
    const double ampMedian = amplitudes.isEmpty() ? features.vcapAmplitude : medianValue(amplitudes);

    features.highRetention = highMedian / clampMin(vinMax, 1e-6);
    features.vcapAmplitude = ampMedian;
    features.tauRiseSec = tauRise.isEmpty() ? 0.0 : medianValue(tauRise);
    features.tauDischargeSec = tauDis.isEmpty() ? 0.0 : medianValue(tauDis);
    features.edgeDrop = edgeDrops.isEmpty() ? 0.0 : medianValue(edgeDrops);

    const double cNominalUf = input.parameters.value(QStringLiteral("cNominal_uF"), 10.0).toDouble();
    const double r1Ohms = input.parameters.value(QStringLiteral("r1Ohms"), 1000.0).toDouble();
    features.nominalTauSec = qMax(0.0, cNominalUf * 1e-6 * r1Ohms);

    bool tempPresent = false;
    const double tmax = firstPresent(input.parameters,
                                     {QStringLiteral("temperatureMaxC"),
                                      QStringLiteral("roiMaxTempC"),
                                      QStringLiteral("tMaxC")},
                                     std::numeric_limits<double>::quiet_NaN(),
                                     &tempPresent);
    if (tempPresent) {
        features.tempMaxC = tmax;
    }

    Q_UNUSED(lowMedian)
    return features;
}

int classifyMode(const DiagnosticInput &input, const CapacitorFeatures &features)
{
    const double vh = input.parameters.value(QStringLiteral("stimulusAmplitudeV"), 5.0).toDouble();
    const double tauTolerance = input.parameters.value(QStringLiteral("tauDriftTolerance"), 0.35).toDouble();

    const double shortAmplitudeThreshold = qMax(0.2, 0.12 * qMax(vh, 0.1));
    const double openTauFactor = 0.30;
    const double openCorrelationThreshold = 0.97;
    const double openNmaeThreshold = 0.18;
    const double leakRetentionThreshold = 0.90;
    const double leakDischargeFactor = 0.60;
    const double esrEdgeDropThreshold = qMax(0.03, 0.06 * qMax(vh, 0.1));

    const bool shortLike = (features.vcapAmplitude <= shortAmplitudeThreshold)
        || (features.vcapMax <= shortAmplitudeThreshold);
    if (shortLike) {
        return 3;
    }

    const bool nominalTauAvailable = features.nominalTauSec > 0.0;
    const bool openByTau = nominalTauAvailable
        && features.tauRiseSec > 0.0
        && features.tauRiseSec < features.nominalTauSec * openTauFactor;
    const bool openByFollow = features.followCorrelation >= openCorrelationThreshold
        && features.followNmae <= openNmaeThreshold;
    if (openByTau || openByFollow) {
        return 2;
    }

    const bool leakByRetention = features.highRetention < leakRetentionThreshold;
    const bool leakByDischarge = nominalTauAvailable
        && features.tauDischargeSec > 0.0
        && features.tauDischargeSec < features.nominalTauSec * leakDischargeFactor;
    const bool leakByEsr = features.edgeDrop > esrEdgeDropThreshold;
    if (leakByRetention || leakByDischarge || leakByEsr) {
        return 4;
    }

    if (nominalTauAvailable && features.tauRiseSec > 0.0) {
        const double ratio = features.tauRiseSec / features.nominalTauSec;
        if (std::fabs(ratio - 1.0) > tauTolerance) {
            return 1;
        }
    }

    return 0;
}

}

CapacitorDiagnosticPlugin::CapacitorDiagnosticPlugin(QObject *parent)
    : QObject(parent)
{
}

QString CapacitorDiagnosticPlugin::pluginId() const
{
    return QStringLiteral("diag.capacitor.mode");
}

QString CapacitorDiagnosticPlugin::displayName() const
{
    return QStringLiteral("Capacitor MODE Diagnostic");
}

QString CapacitorDiagnosticPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QString CapacitorDiagnosticPlugin::componentType() const
{
    return QStringLiteral("tps.capacitor");
}

DiagnosticReport CapacitorDiagnosticPlugin::diagnose(const DiagnosticInput &input, QString *error) const
{
    DiagnosticReport report;
    report.componentRef = input.componentRef;
    report.componentType = input.componentType;

    const CapacitorFeatures features = extractFeatures(input);
    if (features.sampleCount <= 8) {
        report.success = false;
        report.summary = QStringLiteral("FAIL");
        report.detailHtml = QStringLiteral("<p>电容诊断失败：capVoltageInput/vinVoltageInput 采样不足。</p>");
        if (error) {
            *error = QStringLiteral("insufficient signal samples");
        }
        return report;
    }

    const int expected = parseExpectedMode(input.parameters.value(QStringLiteral("expectedMode")), 0);
    const int predicted = classifyMode(input, features);

    const double tWarn = firstPresent(input.parameters,
                                      {QStringLiteral("temperatureWarnC"), QStringLiteral("tWarnC")},
                                      40.0);
    const double tTrip = firstPresent(input.parameters,
                                      {QStringLiteral("temperatureTripC"), QStringLiteral("tTripC")},
                                      60.0);

    bool thermalWarn = false;
    bool thermalTrip = false;
    if (!std::isnan(features.tempMaxC)) {
        thermalWarn = features.tempMaxC >= tWarn;
        thermalTrip = features.tempMaxC >= tTrip;
    }

    const bool modeMatched = (predicted == expected);
    report.success = modeMatched && !thermalTrip;
    report.summary = report.success ? QStringLiteral("PASS") : QStringLiteral("FAIL");

    report.metrics.insert(QStringLiteral("mode.predicted"), predicted);
    report.metrics.insert(QStringLiteral("mode.expected"), expected);
    report.metrics.insert(QStringLiteral("sample.count"), features.sampleCount);
    report.metrics.insert(QStringLiteral("cycle.count"), features.cycleCount);
    report.metrics.insert(QStringLiteral("sampleRateHz"), features.sampleRateHz);
    report.metrics.insert(QStringLiteral("vcap.max"), features.vcapMax);
    report.metrics.insert(QStringLiteral("vcap.min"), features.vcapMin);
    report.metrics.insert(QStringLiteral("vcap.amplitude"), features.vcapAmplitude);
    report.metrics.insert(QStringLiteral("vin.amplitude"), features.vinAmplitude);
    report.metrics.insert(QStringLiteral("high.retention"), features.highRetention);
    report.metrics.insert(QStringLiteral("follow.corr"), features.followCorrelation);
    report.metrics.insert(QStringLiteral("follow.nmae"), features.followNmae);
    report.metrics.insert(QStringLiteral("tau.rise.sec"), features.tauRiseSec);
    report.metrics.insert(QStringLiteral("tau.discharge.sec"), features.tauDischargeSec);
    report.metrics.insert(QStringLiteral("tau.nominal.sec"), features.nominalTauSec);
    report.metrics.insert(QStringLiteral("edge.drop"), features.edgeDrop);
    report.metrics.insert(QStringLiteral("thermal.warn"), thermalWarn);
    report.metrics.insert(QStringLiteral("thermal.trip"), thermalTrip);
    if (!std::isnan(features.tempMaxC)) {
        report.metrics.insert(QStringLiteral("temperature.maxC"), features.tempMaxC);
    }

    QStringList lines;
    lines.push_back(QStringLiteral("预测工况: %1").arg(modeName(predicted)));
    lines.push_back(QStringLiteral("期望工况: %1").arg(modeName(expected)));
    lines.push_back(QStringLiteral("tau(rise/dis/nominal): %1 / %2 / %3 ms")
                        .arg(features.tauRiseSec * 1000.0, 0, 'f', 3)
                        .arg(features.tauDischargeSec * 1000.0, 0, 'f', 3)
                        .arg(features.nominalTauSec * 1000.0, 0, 'f', 3));
    lines.push_back(QStringLiteral("稳态保持比: %1, 跟随度corr: %2, NMAE: %3")
                        .arg(features.highRetention, 0, 'f', 4)
                        .arg(features.followCorrelation, 0, 'f', 4)
                        .arg(features.followNmae, 0, 'f', 4));
    lines.push_back(QStringLiteral("幅值(vcap/vin): %1 / %2 V, 边沿压降: %3 V")
                        .arg(features.vcapAmplitude, 0, 'f', 4)
                        .arg(features.vinAmplitude, 0, 'f', 4)
                        .arg(features.edgeDrop, 0, 'f', 4));

    if (!std::isnan(features.tempMaxC)) {
        lines.push_back(QStringLiteral("温度Tmax: %1 ℃ (Warn/Trip=%2/%3)")
                            .arg(features.tempMaxC, 0, 'f', 2)
                            .arg(tWarn, 0, 'f', 1)
                            .arg(tTrip, 0, 'f', 1));
        if (thermalTrip) {
            lines.push_back(QStringLiteral("温度触发停机阈值，判定为 FAIL。"));
        } else if (thermalWarn) {
            lines.push_back(QStringLiteral("温度达到预警阈值，请记录并检查。"));
        }
    }

    if (!modeMatched) {
        lines.push_back(QStringLiteral("模式不一致：Pred=%1, Exp=%2")
                            .arg(modeName(predicted), modeName(expected)));
    }

    report.detailHtml = QStringLiteral("<p>%1</p>").arg(lines.join(QStringLiteral("<br/>")));
    if (!report.success && error) {
        *error = lines.join(QStringLiteral("; "));
    }
    return report;
}
