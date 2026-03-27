#include "transistordiagnosticplugin.h"

#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace {

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

QVector<double> firstSignalSamples(const DiagnosticInput &input, const QStringList &signalKeys)
{
    for (const QString &key : signalKeys) {
        const auto it = input.signalSeries.constFind(key);
        if (it != input.signalSeries.constEnd() && !it.value().samples.isEmpty()) {
            return it.value().samples;
        }
    }
    return {};
}

double firstSignalRate(const DiagnosticInput &input, const QStringList &signalKeys)
{
    for (const QString &key : signalKeys) {
        const auto it = input.signalSeries.constFind(key);
        if (it != input.signalSeries.constEnd() && it.value().sampleRateHz > 0.0) {
            return it.value().sampleRateHz;
        }
    }
    return 0.0;
}

double meanValues(const QVector<double> &values, const QVector<int> &indices)
{
    if (values.isEmpty() || indices.isEmpty()) {
        return 0.0;
    }
    double sum = 0.0;
    int count = 0;
    for (int index : indices) {
        if (index >= 0 && index < values.size()) {
            sum += values.at(index);
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

QVector<int> buildWindowIndices(int sampleCount,
                                int samplesPerPeriod,
                                int startOffset,
                                int endOffset,
                                int baseOffset)
{
    QVector<int> indices;
    if (sampleCount <= 0 || samplesPerPeriod <= 0 || endOffset <= startOffset) {
        return indices;
    }

    for (int periodBase = 0; periodBase < sampleCount; periodBase += samplesPerPeriod) {
        const int from = periodBase + baseOffset + startOffset;
        const int to = periodBase + baseOffset + endOffset;
        for (int index = from; index < to && index < sampleCount; ++index) {
            if (index >= 0) {
                indices.push_back(index);
            }
        }
    }
    return indices;
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
        return QStringLiteral("MODE1(Beta_Drift)");
    case 2:
        return QStringLiteral("MODE2(Open)");
    case 3:
        return QStringLiteral("MODE3(Short_Breakdown)");
    case 4:
        return QStringLiteral("MODE4(Leakage)");
    }
    return QStringLiteral("MODE?(Unknown)");
}

}

TransistorDiagnosticPlugin::TransistorDiagnosticPlugin(QObject *parent)
    : QObject(parent)
{
}

QString TransistorDiagnosticPlugin::pluginId() const
{
    return QStringLiteral("diag.transistor.mode");
}

QString TransistorDiagnosticPlugin::displayName() const
{
    return QStringLiteral("Transistor MODE Diagnostic");
}

QString TransistorDiagnosticPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QString TransistorDiagnosticPlugin::componentType() const
{
    return QStringLiteral("tps.transistor");
}

DiagnosticReport TransistorDiagnosticPlugin::diagnose(const DiagnosticInput &input, QString *error) const
{
    DiagnosticReport report;
    report.componentRef = input.componentRef;
    report.componentType = input.componentType;

    QVector<double> vc = firstSignalSamples(input,
                                            {QStringLiteral("transistorVcInput"), QStringLiteral("vcSamples")});
    QVector<double> ve = firstSignalSamples(input,
                                            {QStringLiteral("transistorVeInput"), QStringLiteral("veSamples")});
    QVector<double> icSense = firstSignalSamples(input,
                                                 {QStringLiteral("transistorIcSenseInput"), QStringLiteral("icSenseSamples")});
    QVector<double> ibSense = firstSignalSamples(input,
                                                 {QStringLiteral("transistorIbSenseInput"), QStringLiteral("ibSenseSamples")});

    const int count = qMin(qMin(vc.size(), ve.size()), qMin(icSense.size(), ibSense.size()));
    if (count <= 8) {
        report.success = false;
        report.summary = QStringLiteral("FAIL");
        report.detailHtml = QStringLiteral("<p>晶体管诊断失败：缺少有效采样(Vc/Ve/IcSense/IbSense)。</p>");
        if (error) {
            *error = QStringLiteral("missing transistor waveform samples");
        }
        return report;
    }

    vc.resize(count);
    ve.resize(count);
    icSense.resize(count);
    ibSense.resize(count);

    double sampleRateHz = firstSignalRate(input,
                                          {QStringLiteral("transistorVcInput"),
                                           QStringLiteral("transistorVeInput"),
                                           QStringLiteral("transistorIcSenseInput"),
                                           QStringLiteral("transistorIbSenseInput")});
    if (sampleRateHz <= 0.0) {
        sampleRateHz = firstPresent(input.parameters, {QStringLiteral("sampleRateHz")}, 1e6);
    }

    const double rb = qMax(1e-12, firstPresent(input.parameters,
                                               {QStringLiteral("rbOhms")},
                                               20000.0));
    const double icSenseOhms = qMax(1e-12, firstPresent(input.parameters,
                                                        {QStringLiteral("icSenseOhms"), QStringLiteral("rlOhms")},
                                                        100.0));
    const double vcc = firstPresent(input.parameters,
                                    {QStringLiteral("vccV")},
                                    5.0);
    const double periodMs = qMax(0.01, firstPresent(input.parameters,
                                                    {QStringLiteral("periodMs")},
                                                    4.0));
    const double tonMs = qBound(0.001,
                                firstPresent(input.parameters, {QStringLiteral("tonMs")}, 2.0),
                                periodMs - 0.0001);

    const double onStartRatio = qBound(0.0,
                                       firstPresent(input.parameters, {QStringLiteral("onWindowStartRatio")}, 0.6),
                                       1.0);
    const double onEndRatio = qBound(0.0,
                                     firstPresent(input.parameters, {QStringLiteral("onWindowEndRatio")}, 0.9),
                                     1.0);
    const double offStartRatio = qBound(0.0,
                                        firstPresent(input.parameters, {QStringLiteral("offWindowStartRatio")}, 0.2),
                                        1.0);
    const double offEndRatio = qBound(0.0,
                                      firstPresent(input.parameters, {QStringLiteral("offWindowEndRatio")}, 0.8),
                                      1.0);

    const int samplesPerPeriod = qMax(2, qRound((periodMs * 1e-3) * sampleRateHz));
    const int samplesOn = qBound(1, qRound((tonMs * 1e-3) * sampleRateHz), samplesPerPeriod - 1);
    const int samplesOff = qMax(1, samplesPerPeriod - samplesOn);

    const int onStart = qBound(0, qRound(samplesOn * qMin(onStartRatio, onEndRatio)), samplesOn - 1);
    const int onEnd = qBound(onStart + 1, qRound(samplesOn * qMax(onStartRatio, onEndRatio)), samplesOn);

    const int offStart = qBound(0, qRound(samplesOff * qMin(offStartRatio, offEndRatio)), samplesOff - 1);
    const int offEnd = qBound(offStart + 1, qRound(samplesOff * qMax(offStartRatio, offEndRatio)), samplesOff);

    const QVector<int> onIndices = buildWindowIndices(count, samplesPerPeriod, onStart, onEnd, 0);
    const QVector<int> offIndices = buildWindowIndices(count, samplesPerPeriod, offStart, offEnd, samplesOn);

    QVector<double> vce;
    QVector<double> ic;
    QVector<double> ib;
    vce.reserve(count);
    ic.reserve(count);
    ib.reserve(count);
    for (int index = 0; index < count; ++index) {
        const double vceNow = vc.at(index) - ve.at(index);
        const double icNow = icSense.at(index) / icSenseOhms;
        const double ibNow = ibSense.at(index) / rb;
        vce.push_back(vceNow);
        ic.push_back(icNow);
        ib.push_back(ibNow);
    }

    const double icOn = meanValues(ic, onIndices);
    const double ibOn = meanValues(ib, onIndices);
    const double vceSat = meanValues(vce, onIndices);
    const double iOff = std::fabs(meanValues(ic, offIndices));

    double beta = 0.0;
    if (std::fabs(ibOn) > 1e-12) {
        beta = icOn / ibOn;
    }

    const double iOpen = firstPresent(input.parameters,
                                      {QStringLiteral("iOpen_A")},
                                      1e-4);
    const double vShort = firstPresent(input.parameters,
                                       {QStringLiteral("vShort_V")},
                                       0.05);
    const double iLimit = firstPresent(input.parameters,
                                       {QStringLiteral("iLimit_A")},
                                       0.04);
    const double iLeak = firstPresent(input.parameters,
                                      {QStringLiteral("iLeak_A")},
                                      1e-7);
    const double betaNominal = qMax(1e-12, firstPresent(input.parameters,
                                                        {QStringLiteral("betaNominal")},
                                                        100.0));
    const double betaMargin = firstPresent(input.parameters,
                                           {QStringLiteral("betaMargin"), QStringLiteral("deltaBeta")},
                                           0.25);

    int predictedMode = 0;
    const bool openMode = (std::fabs(icOn) <= iOpen) && (vceSat >= 0.8 * vcc);
    const bool shortMode = (vceSat <= vShort) && (std::fabs(icOn) >= iLimit);
    const bool leakMode = iOff >= iLeak;
    const double betaRelDiff = std::fabs(beta - betaNominal) / betaNominal;
    const bool betaDriftMode = betaRelDiff > betaMargin;

    if (openMode) {
        predictedMode = 2;
    } else if (shortMode) {
        predictedMode = 3;
    } else if (leakMode) {
        predictedMode = 4;
    } else if (betaDriftMode) {
        predictedMode = 1;
    }

    const int expectedMode = parseExpectedMode(input.parameters.value(QStringLiteral("expectedMode")), 0);

    bool tempPresent = false;
    const double tMax = firstPresent(input.parameters,
                                     {QStringLiteral("temperatureMaxC"), QStringLiteral("roiMaxTempC"), QStringLiteral("tMaxC")},
                                     std::numeric_limits<double>::quiet_NaN(),
                                     &tempPresent);
    const double tTrip = firstPresent(input.parameters,
                                      {QStringLiteral("temperatureTripC"), QStringLiteral("tTripC")},
                                      60.0);
    const bool thermalTrip = tempPresent && (tMax >= tTrip);

    const bool modeMatched = (predictedMode == expectedMode);
    report.success = modeMatched && !thermalTrip;
    report.summary = report.success ? QStringLiteral("PASS") : QStringLiteral("FAIL");

    report.metrics.insert(QStringLiteral("mode.predicted"), predictedMode);
    report.metrics.insert(QStringLiteral("mode.expected"), expectedMode);
    report.metrics.insert(QStringLiteral("sample.count"), count);
    report.metrics.insert(QStringLiteral("sampleRateHz"), sampleRateHz);
    report.metrics.insert(QStringLiteral("Ic_on_A"), icOn);
    report.metrics.insert(QStringLiteral("Ib_on_A"), ibOn);
    report.metrics.insert(QStringLiteral("beta_est"), beta);
    report.metrics.insert(QStringLiteral("beta.relDiff"), betaRelDiff);
    report.metrics.insert(QStringLiteral("VCE_sat_V"), vceSat);
    report.metrics.insert(QStringLiteral("Ioff_A"), iOff);
    report.metrics.insert(QStringLiteral("Vcc_V"), vcc);
    report.metrics.insert(QStringLiteral("Iopen_A"), iOpen);
    report.metrics.insert(QStringLiteral("Vshort_V"), vShort);
    report.metrics.insert(QStringLiteral("Ilimit_A"), iLimit);
    report.metrics.insert(QStringLiteral("Ileak_A"), iLeak);
    report.metrics.insert(QStringLiteral("betaNominal"), betaNominal);
    report.metrics.insert(QStringLiteral("betaMargin"), betaMargin);
    report.metrics.insert(QStringLiteral("thermal.trip"), thermalTrip);
    if (tempPresent) {
        report.metrics.insert(QStringLiteral("temperature.maxC"), tMax);
        report.metrics.insert(QStringLiteral("temperature.tripC"), tTrip);
    }

    QStringList lines;
    lines.push_back(QStringLiteral("预测工况: %1").arg(modeName(predictedMode)));
    lines.push_back(QStringLiteral("期望工况: %1").arg(modeName(expectedMode)));
    lines.push_back(QStringLiteral("Ic_on=%1 A, Ib_on=%2 A, beta=%3")
                        .arg(icOn, 0, 'g', 8)
                        .arg(ibOn, 0, 'g', 8)
                        .arg(beta, 0, 'g', 8));
    lines.push_back(QStringLiteral("VCEsat=%1 V, Ioff=%2 A")
                        .arg(vceSat, 0, 'g', 8)
                        .arg(iOff, 0, 'g', 8));

    if (tempPresent) {
        lines.push_back(QStringLiteral("温度 Tmax=%1 ℃, Trip=%2 ℃")
                            .arg(tMax, 0, 'f', 2)
                            .arg(tTrip, 0, 'f', 1));
        if (thermalTrip) {
            lines.push_back(QStringLiteral("温度达到停机阈值，判定 FAIL。"));
        }
    }

    if (!modeMatched) {
        lines.push_back(QStringLiteral("模式不一致：Pred=%1, Exp=%2")
                            .arg(modeName(predictedMode), modeName(expectedMode)));
    }

    report.detailHtml = QStringLiteral("<p>%1</p>").arg(lines.join(QStringLiteral("<br/>")));
    if (!report.success && error) {
        *error = lines.join(QStringLiteral("; "));
    }

    return report;
}
