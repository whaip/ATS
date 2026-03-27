#include "inductordiagnosticplugin.h"

#include <cmath>
#include <limits>

namespace {

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

double maxAbsValue(const QVector<double> &values)
{
    double result = 0.0;
    for (double value : values) {
        result = qMax(result, std::fabs(value));
    }
    return result;
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

double nominalLHenry(const QMap<QString, QVariant> &parameters)
{
    if (parameters.contains(QStringLiteral("nominalL_H"))) {
        return parameters.value(QStringLiteral("nominalL_H")).toDouble();
    }
    if (parameters.contains(QStringLiteral("nominalL_mH"))) {
        return parameters.value(QStringLiteral("nominalL_mH")).toDouble() * 1e-3;
    }
    if (parameters.contains(QStringLiteral("nominalL_uH"))) {
        return parameters.value(QStringLiteral("nominalL_uH")).toDouble() * 1e-6;
    }
    return 10e-3;
}

int parseExpectedMode(const QVariant &value, int fallback)
{
    bool ok = false;
    const int direct = value.toInt(&ok);
    if (ok) {
        return qBound(0, direct, 4);
    }

    const QString text = value.toString().trimmed().toUpper();
    if (text.startsWith(QStringLiteral("MODE"))) {
        const int parsed = text.mid(4).trimmed().toInt(&ok);
        if (ok) {
            return qBound(0, parsed, 4);
        }
    }
    return fallback;
}

QString modeName(int mode)
{
    switch (mode) {
    case 0:
        return QStringLiteral("MODE0(Normal)");
    case 1:
        return QStringLiteral("MODE1(L_drift)");
    case 2:
        return QStringLiteral("MODE2(Open)");
    case 3:
        return QStringLiteral("MODE3(Short)");
    case 4:
        return QStringLiteral("MODE4(DCR_degradation)");
    }
    return QStringLiteral("MODE?(Unknown)");
}

}

InductorDiagnosticPlugin::InductorDiagnosticPlugin(QObject *parent)
    : QObject(parent)
{
}

QString InductorDiagnosticPlugin::pluginId() const
{
    return QStringLiteral("diag.inductor.mode");
}

QString InductorDiagnosticPlugin::displayName() const
{
    return QStringLiteral("Inductor MODE Diagnostic");
}

QString InductorDiagnosticPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QString InductorDiagnosticPlugin::componentType() const
{
    return QStringLiteral("tps.inductor");
}

DiagnosticReport InductorDiagnosticPlugin::diagnose(const DiagnosticInput &input, QString *error) const
{
    DiagnosticReport report;
    report.componentRef = input.componentRef;
    report.componentType = input.componentType;

    QVector<double> vin = firstSignalSamples(
        input,
        {QStringLiteral("inductorVinInput"), QStringLiteral("vinSamples"), QStringLiteral("vinInput")});
    QVector<double> vn1 = firstSignalSamples(
        input,
        {QStringLiteral("inductorVn1Input"), QStringLiteral("vn1Samples"), QStringLiteral("vn1Input")});
    QVector<double> vn2 = firstSignalSamples(
        input,
        {QStringLiteral("inductorVn2Input"), QStringLiteral("vn2Samples"), QStringLiteral("vn2Input")});

    const int count = qMin(vin.size(), qMin(vn1.size(), vn2.size()));
    if (count < 8) {
        report.success = false;
        report.summary = QStringLiteral("FAIL");
        report.detailHtml = QStringLiteral("<p>电感诊断失败：缺少有效采样(inductorVinInput/inductorVn1Input/inductorVn2Input)。</p>");
        if (error) {
            *error = QStringLiteral("missing inductor waveform samples");
        }
        return report;
    }
    vin.resize(count);
    vn1.resize(count);
    vn2.resize(count);

    double sampleRateHz = firstSignalRate(input,
                                          {QStringLiteral("inductorVinInput"),
                                           QStringLiteral("inductorVn1Input"),
                                           QStringLiteral("inductorVn2Input")});
    if (sampleRateHz <= 0.0) {
        sampleRateHz = firstPresent(input.parameters,
                                    {QStringLiteral("sampleRateHz")},
                                    20000.0);
    }

    const double rs = firstPresent(input.parameters,
                                   {QStringLiteral("rsOhms")},
                                   10.0);
    const double vh = firstPresent(input.parameters,
                                   {QStringLiteral("Vh"), QStringLiteral("stimulusAmplitudeV")},
                                   5.0);
    const double tonMs = firstPresent(input.parameters,
                                      {QStringLiteral("TonMs"), QStringLiteral("tonMs"), QStringLiteral("pulseHighMs")},
                                      2.0);
    const double tPerMs = firstPresent(input.parameters,
                                       {QStringLiteral("TperMs"), QStringLiteral("periodMs")},
                                       4.0);
    const double t1Ms = firstPresent(input.parameters,
                                     {QStringLiteral("t1Ms")},
                                     0.3);
    const double t2Ms = firstPresent(input.parameters,
                                     {QStringLiteral("t2Ms")},
                                     1.3);
    const double t3Ms = firstPresent(input.parameters,
                                     {QStringLiteral("t3Ms")},
                                     1.8);

    const double lNomH = nominalLHenry(input.parameters);
    const double rdcrNom = firstPresent(input.parameters,
                                        {QStringLiteral("rdcrNomOhms"), QStringLiteral("nominalRdcrOhms"), QStringLiteral("RdcrNom")},
                                        0.2);

    const double deltaL = firstPresent(input.parameters,
                                       {QStringLiteral("deltaLPercent"), QStringLiteral("tolerancePercent")},
                                       20.0) / 100.0;
    const double dcrDegradeFactor = firstPresent(input.parameters,
                                                 {QStringLiteral("dcrDegradeFactor"), QStringLiteral("rdcrDegradeFactor")},
                                                 3.0);
    const double shortCurrentFactor = firstPresent(input.parameters,
                                                   {QStringLiteral("shortCurrentFactor")},
                                                   1.2);
    const double shortLRatio = firstPresent(input.parameters,
                                            {QStringLiteral("shortLRatio")},
                                            0.1);
    const double shortRdcrRatio = firstPresent(input.parameters,
                                               {QStringLiteral("shortRdcrRatio")},
                                               0.2);
    const double openCurrentFloor = firstPresent(input.parameters,
                                                 {QStringLiteral("openCurrentFloorA")},
                                                 0.005);
    const double openSlopeFloor = firstPresent(input.parameters,
                                               {QStringLiteral("openSlopeFloorAps")},
                                               1.0);
    const double epsilonI = firstPresent(input.parameters,
                                         {QStringLiteral("epsilonCurrentA"), QStringLiteral("epsilon")},
                                         1e-6);

    QVector<double> i;
    QVector<double> vL;
    i.reserve(count);
    vL.reserve(count);
    for (int index = 0; index < count; ++index) {
        const double current = (vin.at(index) - vn1.at(index)) / qMax(rs, 1e-12);
        const double inductorVoltage = vn1.at(index) - vn2.at(index);
        i.push_back(current);
        vL.push_back(inductorVoltage);
    }

    const double dt = (sampleRateHz > 0.0) ? (1.0 / sampleRateHz) : 0.0;
    const int idx1 = qBound(0, static_cast<int>(std::round(t1Ms * 1e-3 * sampleRateHz)), count - 1);
    const int idx2 = qBound(idx1 + 1, static_cast<int>(std::round(t2Ms * 1e-3 * sampleRateHz)), count - 1);
    const int idx3 = qBound(0, static_cast<int>(std::round(t3Ms * 1e-3 * sampleRateHz)), count - 1);

    const double i1 = i.at(idx1);
    const double i2 = i.at(idx2);
    const double slopeDiDt = (dt > 0.0 && idx2 > idx1)
        ? ((i2 - i1) / (static_cast<double>(idx2 - idx1) * dt))
        : 0.0;
    const double vLhat = meanRange(vL, idx1, idx2 + 1);
    const double lHatH = (std::fabs(slopeDiDt) > 1e-12)
        ? std::fabs(vLhat / slopeDiDt)
        : std::numeric_limits<double>::infinity();
    const double rdcrHat = std::fabs(vL.at(idx3)) / qMax(std::fabs(i.at(idx3)), epsilonI);

    const double iPeak = maxAbsValue(i);
    const double iMean = meanAll(i);
    const double vLMean = meanAll(vL);

    const double tauNom = lNomH / qMax(rs + rdcrNom, 1e-12);
    const double tonSec = tonMs * 1e-3;
    const double iExpectedPeak = (rs + rdcrNom > 1e-12 && tauNom > 0.0)
        ? (vh / (rs + rdcrNom)) * (1.0 - std::exp(-tonSec / tauNom))
        : 0.0;

    const bool openByCurrent = iPeak <= openCurrentFloor;
    const bool openBySlope = std::fabs(slopeDiDt) <= openSlopeFloor;
    const bool openByInvalidL = !qIsFinite(lHatH) || (lHatH > lNomH * 20.0);
    const bool shortByCurrent = (iExpectedPeak > 1e-9) && (iPeak >= iExpectedPeak * shortCurrentFactor);
    const bool shortByL = qIsFinite(lHatH) && (lHatH <= lNomH * shortLRatio);
    const bool shortByRdcr = qIsFinite(rdcrHat) && (rdcrHat <= rdcrNom * shortRdcrRatio);
    const bool dcrDegraded = qIsFinite(rdcrHat) && (rdcrHat >= rdcrNom * dcrDegradeFactor);

    const double lRelativeError = (lNomH > 1e-12 && qIsFinite(lHatH))
        ? std::fabs(lHatH - lNomH) / lNomH
        : std::numeric_limits<double>::infinity();
    const bool lDrifted = lRelativeError > deltaL;

    int predictedMode = 0;
    if (openByCurrent && (openBySlope || openByInvalidL)) {
        predictedMode = 2;
    } else if (shortByCurrent || shortByL || shortByRdcr) {
        predictedMode = 3;
    } else if (dcrDegraded) {
        predictedMode = 4;
    } else if (lDrifted) {
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
    report.metrics.insert(QStringLiteral("L.nominal.H"), lNomH);
    report.metrics.insert(QStringLiteral("L.estimated.H"), lHatH);
    report.metrics.insert(QStringLiteral("L.relativeError"), lRelativeError);
    report.metrics.insert(QStringLiteral("Rdcr.nominal"), rdcrNom);
    report.metrics.insert(QStringLiteral("Rdcr.estimated"), rdcrHat);
    report.metrics.insert(QStringLiteral("i.peak"), iPeak);
    report.metrics.insert(QStringLiteral("i.mean"), iMean);
    report.metrics.insert(QStringLiteral("vL.mean"), vLMean);
    report.metrics.insert(QStringLiteral("di_dt.estimate"), slopeDiDt);
    report.metrics.insert(QStringLiteral("i.expectedPeak"), iExpectedPeak);
    report.metrics.insert(QStringLiteral("window.t1Ms"), t1Ms);
    report.metrics.insert(QStringLiteral("window.t2Ms"), t2Ms);
    report.metrics.insert(QStringLiteral("window.t3Ms"), t3Ms);
    report.metrics.insert(QStringLiteral("stimulus.Vh"), vh);
    report.metrics.insert(QStringLiteral("stimulus.TonMs"), tonMs);
    report.metrics.insert(QStringLiteral("stimulus.TperMs"), tPerMs);
    report.metrics.insert(QStringLiteral("loop.Rs"), rs);
    report.metrics.insert(QStringLiteral("thermal.trip"), thermalTrip);
    if (tempPresent) {
        report.metrics.insert(QStringLiteral("temperature.maxC"), tMax);
        report.metrics.insert(QStringLiteral("temperature.tripC"), tTrip);
    }

    QStringList lines;
    lines.push_back(QStringLiteral("预测工况: %1").arg(modeName(predictedMode)));
    lines.push_back(QStringLiteral("期望工况: %1").arg(modeName(expectedMode)));
    lines.push_back(QStringLiteral("Lhat/Lnom: %1 / %2 H")
                        .arg(lHatH, 0, 'g', 8)
                        .arg(lNomH, 0, 'g', 8));
    lines.push_back(QStringLiteral("RdcrHat/RdcrNom: %1 / %2 Ω")
                        .arg(rdcrHat, 0, 'g', 8)
                        .arg(rdcrNom, 0, 'g', 8));
    lines.push_back(QStringLiteral("iPeak=%1 A, di/dt=%2 A/s, iExpectedPeak=%3 A")
                        .arg(iPeak, 0, 'g', 8)
                        .arg(slopeDiDt, 0, 'g', 8)
                        .arg(iExpectedPeak, 0, 'g', 8));

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
