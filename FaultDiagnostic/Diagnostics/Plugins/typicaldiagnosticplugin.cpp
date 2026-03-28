#include "typicaldiagnosticplugin.h"

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
                                    QStringLiteral("temperatureTripC"),
                                    QStringLiteral("tWarnC"),
                                    QStringLiteral("tTripC")},
                                   std::numeric_limits<double>::quiet_NaN());
    }
    return true;
}
}

TypicalDiagnosticPlugin::TypicalDiagnosticPlugin(QObject *parent)
    : QObject(parent)
{
}

QString TypicalDiagnosticPlugin::pluginId() const
{
    return QStringLiteral("diag.typical.temperature");
}

QString TypicalDiagnosticPlugin::displayName() const
{
    return QStringLiteral("Typical 温度诊断");
}

QString TypicalDiagnosticPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QString TypicalDiagnosticPlugin::componentType() const
{
    return QStringLiteral("tps.typical");
}

DiagnosticReport TypicalDiagnosticPlugin::diagnose(const DiagnosticInput &input, QString *error) const
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

    bool maxTempPresent = false;
    const double maxTempC = firstPresent(input.parameters,
                                         {QStringLiteral("temperatureMaxC"),
                                          QStringLiteral("roiMaxTempC"),
                                          QStringLiteral("tMaxC")},
                                         std::numeric_limits<double>::quiet_NaN(),
                                         &maxTempPresent);
    const double warnTempC = firstPresent(input.parameters,
                                          {QStringLiteral("temperatureWarnC"),
                                           QStringLiteral("tWarnC")},
                                          60.0);
    const double tripTempC = firstPresent(input.parameters,
                                          {QStringLiteral("temperatureTripC"),
                                           QStringLiteral("tTripC")},
                                          70.0);
    const bool thermalTrip = maxTempPresent && maxTempC >= tripTempC;

    report.success = !thermalTrip;
    report.summary = report.success ? QStringLiteral("PASS") : QStringLiteral("FAIL");
    report.metrics.insert(QStringLiteral("temperature.warnC"), warnTempC);
    report.metrics.insert(QStringLiteral("temperature.tripC"), tripTempC);
    report.metrics.insert(QStringLiteral("thermal.trip"), thermalTrip);
    if (maxTempPresent) {
        report.metrics.insert(QStringLiteral("temperature.maxC"), maxTempC);
    }

    QStringList lines;
    if (maxTempPresent) {
        lines.push_back(QStringLiteral("最高温度：%1 ℃，预警温度：%2 ℃，停机温度：%3 ℃")
                            .arg(maxTempC, 0, 'f', 2)
                            .arg(warnTempC, 0, 'f', 1)
                            .arg(tripTempC, 0, 'f', 1));
    } else {
        lines.push_back(QStringLiteral("未提供温度结果。"));
    }

    if (thermalTrip) {
        lines.push_back(QStringLiteral("温度达到停机阈值，判定测温区域异常，元件已损坏。"));
    } else if (maxTempPresent && maxTempC >= warnTempC) {
        lines.push_back(QStringLiteral("温度已达到预警阈值，请关注器件发热状态。"));
    } else {
        lines.push_back(QStringLiteral("温度未触发停机，测试结果正常。"));
    }

    lines.push_back(QStringLiteral("该插件为纯温度诊断，不采集电信号数据。"));

    report.detailHtml = QStringLiteral("<p>%1</p>").arg(lines.join(QStringLiteral("<br/>")));
    if (!report.success && error) {
        *error = lines.join(QStringLiteral("; "));
    }

    return report;
}
