#include "resistancetpsplugin.h"

#include <QtMath>

ResistanceTpsPlugin::ResistanceTpsPlugin(QObject *parent)
    : QObject(parent)
{
}

QString ResistanceTpsPlugin::pluginId() const
{
    return QStringLiteral("tps.resistance");
}

QString ResistanceTpsPlugin::displayName() const
{
    return QStringLiteral("Resistance Test TPS");
}

QString ResistanceTpsPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVector<TPSParamDefinition> ResistanceTpsPlugin::parameterDefinitions() const
{
    TPSParamDefinition nominal;
    nominal.key = QStringLiteral("nominalOhms");
    nominal.label = QStringLiteral("标称值(Ω)");
    nominal.type = TPSParamType::Double;
    nominal.defaultValue = 1000.0;
    nominal.minValue = 0.0;
    nominal.maxValue = 1e9;
    nominal.stepValue = 1.0;

    TPSParamDefinition tol;
    tol.key = QStringLiteral("tolerancePercent");
    tol.label = QStringLiteral("容差(%)");
    tol.type = TPSParamType::Double;
    tol.defaultValue = 5.0;
    tol.minValue = 0.0;
    tol.maxValue = 100.0;
    tol.stepValue = 0.1;

    return {nominal, tol};
}

bool ResistanceTpsPlugin::configure(const QMap<QString, QVariant> &settings, QString *error)
{
    Q_UNUSED(error)
    m_settings = settings;
    return true;
}

bool ResistanceTpsPlugin::execute(const TPSRequest &request, TPSResult *result, QString *error)
{
    if (!result) {
        if (error) {
            *error = QStringLiteral("result is null");
        }
        return false;
    }

    const double nominalOhms = m_settings.value(QStringLiteral("nominalOhms"), 1000.0).toDouble();
    const double tolerancePercent = m_settings.value(QStringLiteral("tolerancePercent"), 5.0).toDouble();
    const double measuredOhms = nominalOhms * 1.01;
    const double delta = qAbs(measuredOhms - nominalOhms);
    const double limit = nominalOhms * tolerancePercent / 100.0;

    result->runId = request.runId;
    result->success = delta <= limit;
    result->summary = result->success
        ? QStringLiteral("PASS: %1 Ω").arg(measuredOhms, 0, 'f', 2)
        : QStringLiteral("FAIL: %1 Ω").arg(measuredOhms, 0, 'f', 2);

    result->metrics.insert(QStringLiteral("nominalOhms"), nominalOhms);
    result->metrics.insert(QStringLiteral("measuredOhms"), measuredOhms);
    result->metrics.insert(QStringLiteral("tolerancePercent"), tolerancePercent);
    result->metrics.insert(QStringLiteral("items"), request.items.size());
    return true;
}
