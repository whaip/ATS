#include "resistordiagnosticplugin.h"

#include <cmath>
#include <limits>

namespace {

double meanValue(const QVector<double> &values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (double value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
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

bool temperatureAlarmTriggered(const DiagnosticInput &input,
                               double *maxTempC = nullptr,
                               double *thresholdC = nullptr)
{
    const bool triggered = input.parameters.value(QStringLiteral("temperatureAlarmTriggered"), false).toBool();
    if (!triggered) {
        return false;
    }

    if (maxTempC) {
        *maxTempC = firstPresent(input.parameters,
                                 {QStringLiteral("temperatureAlarmMaxC"),
                                  QStringLiteral("temperatureMaxC"),
                                  QStringLiteral("roiMaxTempC"),
                                  QStringLiteral("tMaxC")},
                                 std::numeric_limits<double>::quiet_NaN());
    }
    if (thresholdC) {
        *thresholdC = firstPresent(input.parameters,
                                   {QStringLiteral("temperatureAlarmThresholdC"),
                                    QStringLiteral("temperatureWarnC"),
                                    QStringLiteral("temperatureTripC"),
                                    QStringLiteral("tWarnC"),
                                    QStringLiteral("tTripC")},
                                   std::numeric_limits<double>::quiet_NaN());
    }
    return true;
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

QString modeName(int mode)
{
    switch (mode) {
    case 0:
        return QStringLiteral("MODE0(Normal)");
    case 1:
        return QStringLiteral("MODE1(R_drift)");
    case 2:
        return QStringLiteral("MODE2(Open)");
    case 3:
        return QStringLiteral("MODE3(Short)");
    }
    return QStringLiteral("MODE?(Unknown)");
}

int parseExpectedMode(const QVariant &value, int fallback)
{
    bool ok = false;
    const int direct = value.toInt(&ok);
    if (ok) {
        return qBound(0, direct, 3);
    }

    const QString text = value.toString().trimmed().toUpper();
    if (text.startsWith(QStringLiteral("MODE"))) {
        const QString modeNumber = text.mid(4).trimmed();
        const int parsed = modeNumber.toInt(&ok);
        if (ok) {
            return qBound(0, parsed, 3);
        }
    }

    return fallback;
}

}

ResistorDiagnosticPlugin::ResistorDiagnosticPlugin(QObject *parent)
    : QObject(parent)
{
}

QString ResistorDiagnosticPlugin::pluginId() const
{
    return QStringLiteral("diag.resistor.mode");
}

QString ResistorDiagnosticPlugin::displayName() const
{
    return QStringLiteral("Resistor MODE Diagnostic");
}

QString ResistorDiagnosticPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QString ResistorDiagnosticPlugin::componentType() const
{
    return QStringLiteral("tps.resistance");
}

DiagnosticReport ResistorDiagnosticPlugin::diagnose(const DiagnosticInput &input, QString *error) const
{
    DiagnosticReport report;
    report.componentRef = input.componentRef;
    report.componentType = input.componentType;

    double alarmMaxC = std::numeric_limits<double>::quiet_NaN();
    double alarmThresholdC = std::numeric_limits<double>::quiet_NaN();
    if (temperatureAlarmTriggered(input, &alarmMaxC, &alarmThresholdC)) {
        report.success = false;
        report.summary = QStringLiteral("FAIL");
        report.metrics.insert(QStringLiteral("thermal.alarm"), true);
        if (!std::isnan(alarmMaxC)) {
            report.metrics.insert(QStringLiteral("temperature.maxC"), alarmMaxC);
        }
        if (!std::isnan(alarmThresholdC)) {
            report.metrics.insert(QStringLiteral("temperature.alarmThresholdC"), alarmThresholdC);
        }
        report.detailHtml = QStringLiteral("<p>测温区域异常，元件已损坏，已停止测试。</p>");
        if (error) {
            *error = QStringLiteral("temperature alarm triggered");
        }
        return report;
    }

    const QVector<double> voltageSamples = firstSignalSamples(
        input,
        {QStringLiteral("resistanceMeasurementInput"),
         QStringLiteral("voltageIn1"),
         QStringLiteral("resistanceVoltageInput")});

    if (voltageSamples.isEmpty()) {
        report.success = false;
        report.summary = QStringLiteral("FAIL");
        report.detailHtml = QStringLiteral("<p>电阻诊断失败：缺少电压采样(resistanceMeasurementInput)。</p>");
        if (error) {
            *error = QStringLiteral("missing resistance voltage samples");
        }
        return report;
    }

    const QVector<double> currentSamples = firstSignalSamples(
        input,
        {QStringLiteral("resistanceMeasurementCurrent"),
         QStringLiteral("currentIn1"),
         QStringLiteral("resistanceCurrentInput")});

    const double rNom = firstPresent(input.parameters,
                                     {QStringLiteral("nominalOhms"), QStringLiteral("rNomOhms")},
                                     1000.0);
    const double iTest = firstPresent(input.parameters,
                                      {QStringLiteral("testCurrentA"), QStringLiteral("iTestA")},
                                      0.004);
    const double vMax = firstPresent(input.parameters,
                                     {QStringLiteral("complianceVoltageV"), QStringLiteral("vMax"), QStringLiteral("vMaxV")},
                                     12.0);

    const double tolerancePercent = firstPresent(input.parameters,
                                                 {QStringLiteral("tolerancePercent")},
                                                 10.0);
    const double measurementMarginPercent = firstPresent(input.parameters,
                                                         {QStringLiteral("measurementMarginPercent"), QStringLiteral("extraTolerancePercent")},
                                                         2.0);

    const double alpha = firstPresent(input.parameters,
                                      {QStringLiteral("openAlpha")},
                                      0.96);
    const double beta = firstPresent(input.parameters,
                                     {QStringLiteral("openBeta")},
                                     0.2);

    const double vShort = firstPresent(input.parameters,
                                       {QStringLiteral("vShortV"), QStringLiteral("shortVoltageThresholdV")},
                                       0.01);
    const double rShortRatio = firstPresent(input.parameters,
                                            {QStringLiteral("rShortRatio")},
                                            0.05);

    const double measuredVrMean = meanValue(voltageSamples);
    const double measuredVrAbs = maxAbsValue(voltageSamples);

    const double estimatedCurrent = currentSamples.isEmpty() ? iTest : meanValue(currentSamples);
    const double absCurrent = std::fabs(estimatedCurrent);
    const double currentForR = qMax(absCurrent, 1e-12);
    const double rHat = measuredVrAbs / currentForR;

    const int expectedMode = parseExpectedMode(input.parameters.value(QStringLiteral("expectedMode")), 0);

    int predictedMode = 0;
    const bool openByVoltage = measuredVrAbs >= alpha * vMax;
    const bool openByCurrent = absCurrent <= beta * iTest;
    const bool openByR = rHat >= qMax(rNom * 5.0, vMax / qMax(iTest, 1e-12));

    const bool shortByVoltage = measuredVrAbs <= vShort;
    const bool shortByR = rHat <= rNom * rShortRatio;

    if (shortByVoltage || shortByR) {
        predictedMode = 3;
    } else if (openByVoltage && (openByCurrent || openByR)) {
        predictedMode = 2;
    } else {
        const double delta = (tolerancePercent + measurementMarginPercent) / 100.0;
        const double relativeError = (rNom > 1e-12) ? std::fabs(rHat - rNom) / rNom : 0.0;
        if (relativeError > delta) {
            predictedMode = 1;
        }
    }

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
    report.metrics.insert(QStringLiteral("sample.count"), voltageSamples.size());
    report.metrics.insert(QStringLiteral("Vr.mean"), measuredVrMean);
    report.metrics.insert(QStringLiteral("Vr.absMax"), measuredVrAbs);
    report.metrics.insert(QStringLiteral("I.est"), estimatedCurrent);
    report.metrics.insert(QStringLiteral("R.nominal"), rNom);
    report.metrics.insert(QStringLiteral("R.estimated"), rHat);
    report.metrics.insert(QStringLiteral("V.max"), vMax);
    report.metrics.insert(QStringLiteral("I.test"), iTest);
    report.metrics.insert(QStringLiteral("open.alpha"), alpha);
    report.metrics.insert(QStringLiteral("open.beta"), beta);
    report.metrics.insert(QStringLiteral("short.vThreshold"), vShort);
    report.metrics.insert(QStringLiteral("thermal.trip"), thermalTrip);
    if (tempPresent) {
        report.metrics.insert(QStringLiteral("temperature.maxC"), tMax);
        report.metrics.insert(QStringLiteral("temperature.tripC"), tTrip);
    }

    QStringList lines;
    lines.push_back(QStringLiteral("预测工况: %1").arg(modeName(predictedMode)));
    lines.push_back(QStringLiteral("期望工况: %1").arg(modeName(expectedMode)));
    lines.push_back(QStringLiteral("Vr(absMax/mean): %1 / %2 V")
                        .arg(measuredVrAbs, 0, 'f', 6)
                        .arg(measuredVrMean, 0, 'f', 6));
    lines.push_back(QStringLiteral("Rhat: %1 Ω, Rnom: %2 Ω")
                        .arg(rHat, 0, 'f', 6)
                        .arg(rNom, 0, 'f', 6));
    lines.push_back(QStringLiteral("Iest: %1 A, Itest: %2 A, Vmax: %3 V")
                        .arg(estimatedCurrent, 0, 'g', 8)
                        .arg(iTest, 0, 'g', 8)
                        .arg(vMax, 0, 'f', 3));

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
