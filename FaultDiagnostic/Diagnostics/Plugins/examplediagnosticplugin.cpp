#include "examplediagnosticplugin.h"

#include <QtMath>

#include <cmath>
#include <limits>

namespace {
struct WaveMatchResult {
    double meanMeasured = 0.0;
    double meanExpected = 0.0;
    double meanAbsError = 0.0;
    double maxAbsError = 0.0;
    double tolerance = 0.0;
    bool ok = false;
};

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

double toleranceLimit(double reference, double tolerancePercent)
{
    return qMax(1e-6, qAbs(reference) * qMax(0.0, tolerancePercent) / 100.0);
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

double pulseSample(double lowValue,
                   double highValue,
                   double delaySec,
                   double onSec,
                   double periodSec,
                   int sampleIndex,
                   double sampleRateHz)
{
    if (sampleRateHz <= 0.0 || periodSec <= 0.0) {
        return lowValue;
    }

    const int samplesPerPeriod = qMax(1, qRound(periodSec * sampleRateHz));
    const int samplesDelay = qBound(0, qRound(delaySec * sampleRateHz), samplesPerPeriod);
    const int samplesOn = qBound(0, qRound(onSec * sampleRateHz), qMax(0, samplesPerPeriod - samplesDelay));
    const int position = sampleIndex % samplesPerPeriod;
    return (position >= samplesDelay && position < samplesDelay + samplesOn) ? highValue : lowValue;
}

WaveMatchResult comparePulseWave(const DiagnosticSignalSeries &series,
                                 double highValue,
                                 double delaySec,
                                 double onSec,
                                 double periodSec,
                                 double tolerancePercent)
{
    WaveMatchResult result;
    if (series.samples.isEmpty()) {
        return result;
    }

    const double sampleRateHz = series.sampleRateHz > 0.0 ? series.sampleRateHz : 1000.0;
    const QVector<double> &samples = series.samples;
    double measuredSum = 0.0;
    double expectedSum = 0.0;
    double errorSum = 0.0;
    double maxError = 0.0;

    for (int i = 0; i < samples.size(); ++i) {
        const double expected = pulseSample(0.0, highValue, delaySec, onSec, periodSec, i, sampleRateHz);
        const double error = qAbs(samples.at(i) - expected);
        measuredSum += samples.at(i);
        expectedSum += expected;
        errorSum += error;
        maxError = qMax(maxError, error);
    }

    result.meanMeasured = measuredSum / static_cast<double>(samples.size());
    result.meanExpected = expectedSum / static_cast<double>(samples.size());
    result.meanAbsError = errorSum / static_cast<double>(samples.size());
    result.maxAbsError = maxError;

    const double amplitude = qAbs(highValue);
    const double meanTolerance = toleranceLimit(amplitude > 0.0 ? amplitude : 1.0, tolerancePercent);
    result.tolerance = meanTolerance;
    result.ok = result.meanAbsError <= meanTolerance;
    return result;
}
}

ExampleDiagnosticPlugin::ExampleDiagnosticPlugin(QObject *parent)
    : QObject(parent)
{
}

QString ExampleDiagnosticPlugin::pluginId() const
{
    return QStringLiteral("diag.example.tps.basic");
}

QString ExampleDiagnosticPlugin::displayName() const
{
    return QStringLiteral("Example TPS Diagnostic");
}

QString ExampleDiagnosticPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QString ExampleDiagnosticPlugin::componentType() const
{
    return QStringLiteral("example.tps.basic");
}

DiagnosticReport ExampleDiagnosticPlugin::diagnose(const DiagnosticInput &input, QString *error) const
{
    DiagnosticReport report;
    report.componentRef = input.componentRef;
    report.componentType = input.componentType;

    const double expectedVoltage = input.parameters.value(QStringLiteral("outputVoltageValue"), 5.0).toDouble();
    const double expectedCurrent = input.parameters.value(QStringLiteral("outputCurrentValue"), 0.1).toDouble();
    const double voltagePulseDelay = input.parameters.value(QStringLiteral("voltagePulseTDelay"), 0.0).toDouble();
    const double voltagePulseOn = input.parameters.value(QStringLiteral("voltagePulseTOn"), 0.2).toDouble();
    const double voltagePulsePeriod = input.parameters.value(QStringLiteral("voltagePulseTPeriod"), 1.0).toDouble();
    const double currentPulseDelay = input.parameters.value(QStringLiteral("currentPulseTDelay"), 0.0).toDouble();
    const double currentPulseOn = input.parameters.value(QStringLiteral("currentPulseTOn"), 0.2).toDouble();
    const double currentPulsePeriod = input.parameters.value(QStringLiteral("currentPulseTPeriod"), 1.0).toDouble();
    const double tolerancePercent = input.parameters.value(QStringLiteral("tolerancePercent"), 5.0).toDouble();

    const DiagnosticSignalSeries voltageSeries = input.signalSeries.value(QStringLiteral("exampleVoltageInput"));
    const DiagnosticSignalSeries currentSeries = input.signalSeries.value(QStringLiteral("exampleCurrentInput"));

    const bool hasVoltage = !voltageSeries.samples.isEmpty();
    const bool hasCurrent = !currentSeries.samples.isEmpty();
    if (!hasVoltage || !hasCurrent) {
        report.success = false;
        report.summary = QStringLiteral("FAIL");
        QStringList missing;
        if (!hasVoltage) {
            missing.push_back(QStringLiteral("exampleVoltageInput"));
        }
        if (!hasCurrent) {
            missing.push_back(QStringLiteral("exampleCurrentInput"));
        }
        report.detailHtml = QStringLiteral("<p>Missing input samples: %1</p>")
                                .arg(missing.join(QStringLiteral(", ")));
        if (error) {
            *error = QStringLiteral("missing input samples");
        }
        return report;
    }

    const WaveMatchResult voltageMatch = comparePulseWave(voltageSeries,
                                                          expectedVoltage,
                                                          voltagePulseDelay,
                                                          voltagePulseOn,
                                                          voltagePulsePeriod,
                                                          tolerancePercent);
    const WaveMatchResult currentMatch = comparePulseWave(currentSeries,
                                                          expectedCurrent,
                                                          currentPulseDelay,
                                                          currentPulseOn,
                                                          currentPulsePeriod,
                                                          tolerancePercent);

    bool maxTempPresent = false;
    const double maxTempC = firstPresent(input.parameters,
                                         {QStringLiteral("temperatureMaxC"),
                                          QStringLiteral("roiMaxTempC"),
                                          QStringLiteral("tMaxC"),
                                          QStringLiteral("temperatureAlarmMaxC")},
                                         std::numeric_limits<double>::quiet_NaN(),
                                         &maxTempPresent);
    const double warnTempC = firstPresent(input.parameters,
                                          {QStringLiteral("temperatureWarnC"),
                                           QStringLiteral("tWarnC")},
                                          80.0);
    const double tripTempC = firstPresent(input.parameters,
                                          {QStringLiteral("temperatureTripC"),
                                           QStringLiteral("tTripC")},
                                          100.0);

    double alarmMaxC = std::numeric_limits<double>::quiet_NaN();
    double alarmThresholdC = std::numeric_limits<double>::quiet_NaN();
    const bool thermalAlarm = temperatureAlarmTriggered(input, &alarmMaxC, &alarmThresholdC);
    const bool thermalTrip = maxTempPresent && maxTempC >= tripTempC;
    const bool thermalWarn = maxTempPresent && maxTempC >= warnTempC;

    const bool pass = voltageMatch.ok && currentMatch.ok && !thermalAlarm && !thermalTrip;

    report.success = pass;
    report.summary = pass ? QStringLiteral("PASS") : QStringLiteral("FAIL");
    report.metrics.insert(QStringLiteral("voltage.expectedMean"), voltageMatch.meanExpected);
    report.metrics.insert(QStringLiteral("voltage.measuredMean"), voltageMatch.meanMeasured);
    report.metrics.insert(QStringLiteral("voltage.meanAbsError"), voltageMatch.meanAbsError);
    report.metrics.insert(QStringLiteral("voltage.maxAbsError"), voltageMatch.maxAbsError);
    report.metrics.insert(QStringLiteral("voltage.tolerance"), voltageMatch.tolerance);
    report.metrics.insert(QStringLiteral("voltage.match"), voltageMatch.ok);
    report.metrics.insert(QStringLiteral("current.expectedMean"), currentMatch.meanExpected);
    report.metrics.insert(QStringLiteral("current.measuredMean"), currentMatch.meanMeasured);
    report.metrics.insert(QStringLiteral("current.meanAbsError"), currentMatch.meanAbsError);
    report.metrics.insert(QStringLiteral("current.maxAbsError"), currentMatch.maxAbsError);
    report.metrics.insert(QStringLiteral("current.tolerance"), currentMatch.tolerance);
    report.metrics.insert(QStringLiteral("current.match"), currentMatch.ok);
    report.metrics.insert(QStringLiteral("tolerancePercent"), tolerancePercent);
    report.metrics.insert(QStringLiteral("thermal.warnC"), warnTempC);
    report.metrics.insert(QStringLiteral("thermal.tripC"), tripTempC);
    report.metrics.insert(QStringLiteral("thermal.warn"), thermalWarn);
    report.metrics.insert(QStringLiteral("thermal.trip"), thermalTrip || thermalAlarm);
    if (maxTempPresent) {
        report.metrics.insert(QStringLiteral("temperature.maxC"), maxTempC);
    }
    if (!std::isnan(alarmThresholdC)) {
        report.metrics.insert(QStringLiteral("temperature.alarmThresholdC"), alarmThresholdC);
    }
    if (!std::isnan(alarmMaxC)) {
        report.metrics.insert(QStringLiteral("temperature.alarmMaxC"), alarmMaxC);
    }

    QStringList lines;
    lines.push_back(QStringLiteral("Voltage wave mean expected/measured: %1 / %2 V")
                        .arg(voltageMatch.meanExpected, 0, 'f', 4)
                        .arg(voltageMatch.meanMeasured, 0, 'f', 4));
    lines.push_back(QStringLiteral("Voltage wave mean/max abs error: %1 / %2 V")
                        .arg(voltageMatch.meanAbsError, 0, 'f', 4)
                        .arg(voltageMatch.maxAbsError, 0, 'f', 4));
    lines.push_back(QStringLiteral("Current wave mean expected/measured: %1 / %2 A")
                        .arg(currentMatch.meanExpected, 0, 'f', 4)
                        .arg(currentMatch.meanMeasured, 0, 'f', 4));
    lines.push_back(QStringLiteral("Current wave mean/max abs error: %1 / %2 A")
                        .arg(currentMatch.meanAbsError, 0, 'f', 4)
                        .arg(currentMatch.maxAbsError, 0, 'f', 4));
    lines.push_back(QStringLiteral("Tolerance: %1 %").arg(tolerancePercent, 0, 'f', 2));

    if (maxTempPresent) {
        lines.push_back(QStringLiteral("Temperature Tmax/Warn/Trip: %1 / %2 / %3 C")
                            .arg(maxTempC, 0, 'f', 2)
                            .arg(warnTempC, 0, 'f', 1)
                            .arg(tripTempC, 0, 'f', 1));
    } else {
        lines.push_back(QStringLiteral("Temperature result not provided."));
    }

    if (!voltageMatch.ok) {
        lines.push_back(QStringLiteral("Captured voltage waveform does not match the generated pulse."));
    }
    if (!currentMatch.ok) {
        lines.push_back(QStringLiteral("Captured current waveform does not match the generated pulse."));
    }
    if (thermalAlarm) {
        lines.push_back(QStringLiteral("Temperature alarm triggered during the test."));
    } else if (thermalTrip) {
        lines.push_back(QStringLiteral("Temperature reached the trip threshold."));
    } else if (thermalWarn) {
        lines.push_back(QStringLiteral("Temperature reached the warning threshold."));
    }
    if (pass) {
        lines.push_back(QStringLiteral("Captured waveforms match the configured generated waveforms."));
    }

    report.detailHtml = QStringLiteral("<p>%1</p>").arg(lines.join(QStringLiteral("<br/>")));
    if (!pass && error) {
        *error = lines.join(QStringLiteral("; "));
    }
    return report;
}
