#include "potentiometerdiagnosticplugin.h"

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
                                    QStringLiteral("temperatureTripC")},
                                   std::numeric_limits<double>::quiet_NaN());
    }
    return true;
}
}

PotentiometerDiagnosticPlugin::PotentiometerDiagnosticPlugin(QObject *parent)
    : QObject(parent)
{
}

QString PotentiometerDiagnosticPlugin::pluginId() const
{
    return QStringLiteral("diag.potentiometer");
}

QString PotentiometerDiagnosticPlugin::displayName() const
{
    return QStringLiteral("Potentiometer Diagnostic");
}

QString PotentiometerDiagnosticPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QString PotentiometerDiagnosticPlugin::componentType() const
{
    return QStringLiteral("tps.potentiometer");
}

DiagnosticReport PotentiometerDiagnosticPlugin::diagnose(const DiagnosticInput &input, QString *error) const
{
    DiagnosticReport report;
    report.componentRef = input.componentRef;
    report.componentType = input.componentType;

    double alarmMaxC = std::numeric_limits<double>::quiet_NaN();
    double alarmThresholdC = std::numeric_limits<double>::quiet_NaN();
    const bool thermalAlarm = temperatureAlarmTriggered(input, &alarmMaxC, &alarmThresholdC);

    const double warnTempC = firstPresent(input.parameters,
                                          {QStringLiteral("temperatureWarnC"), QStringLiteral("tWarnC")},
                                          60.0);
    const double tripTempC = firstPresent(input.parameters,
                                          {QStringLiteral("temperatureTripC"), QStringLiteral("tTripC")},
                                          80.0);
    bool maxTempPresent = false;
    const double maxTempC = firstPresent(input.parameters,
                                         {QStringLiteral("temperatureMaxC"),
                                          QStringLiteral("roiMaxTempC"),
                                          QStringLiteral("tMaxC"),
                                          QStringLiteral("temperatureAlarmMaxC")},
                                         std::numeric_limits<double>::quiet_NaN(),
                                         &maxTempPresent);
    const bool thermalTrip = maxTempPresent && maxTempC >= tripTempC;

    report.success = !(thermalAlarm || thermalTrip);
    report.summary = report.success ? QStringLiteral("PASS") : QStringLiteral("FAIL");
    report.metrics.insert(QStringLiteral("thermal.warnC"), warnTempC);
    report.metrics.insert(QStringLiteral("thermal.tripC"), tripTempC);
    report.metrics.insert(QStringLiteral("thermal.trip"), thermalAlarm || thermalTrip);
    if (maxTempPresent) {
        report.metrics.insert(QStringLiteral("temperature.maxC"), maxTempC);
    }
    if (!std::isnan(alarmThresholdC)) {
        report.metrics.insert(QStringLiteral("temperature.alarmThresholdC"), alarmThresholdC);
    }

    QStringList lines;
    lines.push_back(QStringLiteral("potentiometer diagnostic framework created."));
    lines.push_back(QStringLiteral("Detailed diagnostic rules will be implemented later."));
    if (maxTempPresent) {
        lines.push_back(QStringLiteral("Temperature Tmax/Warn/Trip: %1 / %2 / %3 C")
                            .arg(maxTempC, 0, 'f', 2)
                            .arg(warnTempC, 0, 'f', 1)
                            .arg(tripTempC, 0, 'f', 1));
    }
    if (thermalAlarm) {
        lines.push_back(QStringLiteral("Temperature alarm triggered during the test."));
    } else if (thermalTrip) {
        lines.push_back(QStringLiteral("Temperature reached the trip threshold."));
    }

    report.detailHtml = QStringLiteral("<p>%1</p>").arg(lines.join(QStringLiteral("<br/>")));
    if (!report.success && error) {
        *error = lines.join(QStringLiteral("; "));
    }
    return report;
}
