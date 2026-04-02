#include "relaytpsplugin.h"

#include "../../../IODevices/JYDevices/jydeviceconfigutils.h"

namespace {
QString anchorText(const QMap<QString, QVariant> &settings, const QString &key)
{
    const QString text = settings.value(key).toString().trimmed();
    return text.isEmpty() ? QStringLiteral("(unassigned)") : text;
}
}

RelayTpsPlugin::RelayTpsPlugin(QObject *parent)
    : QObject(parent)
{
}

QString RelayTpsPlugin::pluginId() const { return QStringLiteral("tps.relay"); }
QString RelayTpsPlugin::displayName() const { return QStringLiteral("Relay TPS"); }
QString RelayTpsPlugin::version() const { return QStringLiteral("1.0.0"); }
QVector<TPSParamDefinition> RelayTpsPlugin::parameterDefinitions() const { return requirements().parameters; }

TPSPluginRequirement RelayTpsPlugin::requirements() const
{
    TPSPluginRequirement requirement;
    TPSParamDefinition temperatureAnchor{QStringLiteral("temperatureAnchor"), QStringLiteral("Temperature Anchor"), TPSParamType::Integer, -1};
    TPSParamDefinition captureMs; captureMs.key=QStringLiteral("captureDurationMs"); captureMs.label=QStringLiteral("Capture Duration"); captureMs.type=TPSParamType::Integer; captureMs.defaultValue=1000; captureMs.minValue=1; captureMs.maxValue=60000; captureMs.stepValue=10; captureMs.unit=QStringLiteral("ms");
    TPSParamDefinition temperatureWarn; temperatureWarn.key=QStringLiteral("temperatureWarnC"); temperatureWarn.label=QStringLiteral("Temperature Warning"); temperatureWarn.type=TPSParamType::Double; temperatureWarn.defaultValue=60.0; temperatureWarn.minValue=-40.0; temperatureWarn.maxValue=250.0; temperatureWarn.stepValue=0.1; temperatureWarn.unit=QStringLiteral("C");
    TPSParamDefinition temperatureTrip; temperatureTrip.key=QStringLiteral("temperatureTripC"); temperatureTrip.label=QStringLiteral("Temperature Trip"); temperatureTrip.type=TPSParamType::Double; temperatureTrip.defaultValue=80.0; temperatureTrip.minValue=-40.0; temperatureTrip.maxValue=250.0; temperatureTrip.stepValue=0.1; temperatureTrip.unit=QStringLiteral("C");
    requirement.parameters = {temperatureAnchor, captureMs, temperatureWarn, temperatureTrip};
    return requirement;
}

bool RelayTpsPlugin::buildDevicePlan(const QVector<TPSPortBinding> &bindings, const QMap<QString, QVariant> &settings, TPSDevicePlan *plan, QString *error)
{
    if (!plan) { if (error) *error = QStringLiteral("plan is null"); return false; }
    TPSDevicePlan devicePlan;
    devicePlan.bindings = bindings;
    devicePlan.cfg532x = build532xInitConfig(JYDeviceKind::PXIe5322); devicePlan.cfg532x.cfg532x.channelCount = 0; devicePlan.cfg532x.cfg532x.slotNumber = -1;
    devicePlan.cfg5711 = build5711InitConfig(); devicePlan.cfg5711.cfg5711.channelCount = 0; devicePlan.cfg5711.cfg5711.enabledChannels.clear(); devicePlan.cfg5711.cfg5711.waveforms.clear(); devicePlan.cfg5711.cfg5711.slotNumber = -1;
    devicePlan.cfg8902 = build8902InitConfig(); devicePlan.cfg8902.cfg8902.sampleCount = 0; devicePlan.cfg8902.cfg8902.slotNumber = -1;
    devicePlan.wiringSteps = {QStringLiteral("relay TPS framework created. Wiring strategy will be configured later.")};
    devicePlan.temperatureGuide = QStringLiteral("Select the temperature ROI near %1 for relay.").arg(anchorText(settings, QStringLiteral("temperatureAnchor")));
    devicePlan.guide.wiringSteps = devicePlan.wiringSteps;
    devicePlan.guide.roiSteps = {devicePlan.temperatureGuide};
    devicePlan.guide.extensions.insert(QStringLiteral("pluginId"), pluginId());
    devicePlan.guide.extensions.insert(QStringLiteral("roiFocusTargets"), QStringList{anchorText(settings, QStringLiteral("temperatureAnchor"))});
    devicePlan.guide.extensions.insert(QStringLiteral("default.temperatureWarnC"), settings.value(QStringLiteral("temperatureWarnC"), 60.0).toDouble());
    devicePlan.guide.extensions.insert(QStringLiteral("default.temperatureTripC"), settings.value(QStringLiteral("temperatureTripC"), 80.0).toDouble());
    *plan = devicePlan; return true;
}

bool RelayTpsPlugin::configure(const QMap<QString, QVariant> &settings, QString *error)
{
    Q_UNUSED(error)
    m_settings = settings;
    return true;
}

bool RelayTpsPlugin::execute(const TPSRequest &request, TPSResult *result, QString *error)
{
    if (!result) { if (error) *error = QStringLiteral("result is null"); return false; }
    result->runId = request.runId;
    result->success = true;
    result->summary = QStringLiteral("relay TPS framework executed");
    result->metrics.insert(QStringLiteral("captureDurationMs"), m_settings.value(QStringLiteral("captureDurationMs"), 1000).toInt());
    result->metrics.insert(QStringLiteral("temperatureWarnC"), m_settings.value(QStringLiteral("temperatureWarnC"), 60.0).toDouble());
    result->metrics.insert(QStringLiteral("temperatureTripC"), m_settings.value(QStringLiteral("temperatureTripC"), 80.0).toDouble());
    result->metrics.insert(QStringLiteral("items"), request.items.size());
    return true;
}

