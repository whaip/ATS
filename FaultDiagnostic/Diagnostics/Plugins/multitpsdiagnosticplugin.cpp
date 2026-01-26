#include "multitpsdiagnosticplugin.h"

#include <QtMath>

namespace {
struct EvalResult {
    bool ok = true;
    QStringList messages;
};

double meanValue(const QVector<double> &values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (double v : values) {
        sum += v;
    }
    return sum / static_cast<double>(values.size());
}

EvalResult evaluateSignal(const QString &key,
                          const DiagnosticSignalSeries &series,
                          double reference,
                          double tolerancePercent)
{
    EvalResult result;
    if (series.samples.isEmpty()) {
        result.ok = false;
        result.messages.push_back(QStringLiteral("%1 无采样数据").arg(key));
        return result;
    }

    const double avg = meanValue(series.samples);
    if (tolerancePercent < 0.0) {
        tolerancePercent = 0.0;
    }
    const double limit = qAbs(reference) * tolerancePercent / 100.0;
    const double delta = qAbs(avg - reference);
    if (delta > limit) {
        result.ok = false;
        result.messages.push_back(QStringLiteral("%1 超差 (avg=%2 ref=%3)")
                                      .arg(key)
                                      .arg(avg, 0, 'f', 4)
                                      .arg(reference, 0, 'f', 4));
    }
    return result;
}
}

MultiTpsDiagnosticPlugin::MultiTpsDiagnosticPlugin(QObject *parent)
    : QObject(parent)
{
}

QString MultiTpsDiagnosticPlugin::pluginId() const
{
    return QStringLiteral("diag.multi.signal");
}

QString MultiTpsDiagnosticPlugin::displayName() const
{
    return QStringLiteral("Multi TPS Diagnostic");
}

QString MultiTpsDiagnosticPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QString MultiTpsDiagnosticPlugin::componentType() const
{
    return QStringLiteral("tps.multi.signal");
}

DiagnosticReport MultiTpsDiagnosticPlugin::diagnose(const DiagnosticInput &input, QString *error) const
{
    DiagnosticReport report;
    report.componentRef = input.componentRef;
    report.componentType = input.componentType;

    const double tolerancePercent = input.parameters.value(QStringLiteral("tolerancePercent"), 5.0).toDouble();

    const QStringList referenceKeys = {
        QStringLiteral("currentIn1.reference"),
        QStringLiteral("voltageIn1.reference"),
        QStringLiteral("voltageIn2.reference")
    };

    bool allOk = true;
    QStringList messages;

    for (const auto &refKey : referenceKeys) {
        if (!input.parameters.contains(refKey)) {
            continue;
        }
        const double reference = input.parameters.value(refKey).toDouble();
        const QString identifier = refKey.left(refKey.indexOf('.'));
        const auto series = input.signalSeries.value(identifier);
        const auto eval = evaluateSignal(identifier, series, reference, tolerancePercent);
        if (!eval.ok) {
            allOk = false;
        }
        messages.append(eval.messages);
        report.metrics.insert(QStringLiteral("%1.avg").arg(identifier), meanValue(series.samples));
    }

    if (messages.isEmpty()) {
        messages.push_back(QStringLiteral("诊断完成"));
    }

    report.success = allOk;
    report.summary = allOk ? QStringLiteral("PASS") : QStringLiteral("FAIL");
    report.detailHtml = QStringLiteral("<p>%1</p>").arg(messages.join(QStringLiteral("<br/>")));

    if (!allOk && error) {
        *error = messages.join(QStringLiteral("; "));
    }

    return report;
}
