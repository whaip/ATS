#include "exampletpsplugin.h"

#include "../../../IODevices/JYDevices/jydeviceconfigutils.h"

QString ExampleTpsPlugin::pluginId() const
{
    return QStringLiteral("example.tps.basic");
}

QString ExampleTpsPlugin::displayName() const
{
    return QStringLiteral("Example TPS Plugin");
}

QString ExampleTpsPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVector<TPSParamDefinition> ExampleTpsPlugin::parameterDefinitions() const
{
    return requirements().parameters;
}

TPSPluginRequirement ExampleTpsPlugin::requirements() const
{
    TPSPluginRequirement requirement;

    TPSParamDefinition note;
    note.key = QStringLiteral("note");
    note.label = QStringLiteral("备注");
    note.type = TPSParamType::String;
    note.defaultValue = QString();
    note.required = false;

    TPSParamDefinition retry;
    retry.key = QStringLiteral("retryCount");
    retry.label = QStringLiteral("重试次数");
    retry.type = TPSParamType::Integer;
    retry.defaultValue = 0;
    retry.minValue = 0;
    retry.maxValue = 5;
    retry.stepValue = 1;

    requirement.parameters = {note, retry};
    return requirement;
}

bool ExampleTpsPlugin::buildDevicePlan(const QVector<TPSPortBinding> &bindings,
                                       const QMap<QString, QVariant> &settings,
                                       TPSDevicePlan *plan,
                                       QString *error)
{
    Q_UNUSED(settings)
    if (!plan) {
        if (error) {
            *error = QStringLiteral("plan is null");
        }
        return false;
    }

    TPSDevicePlan devicePlan;
    devicePlan.bindings = bindings;
    devicePlan.cfg532x = build532xInitConfig(JYDeviceKind::PXIe5322);
    devicePlan.cfg5711 = build5711InitConfig();
    devicePlan.cfg8902 = build8902InitConfig();
    devicePlan.cfg532x.cfg532x.channelCount = 0;
    devicePlan.cfg532x.cfg532x.slotNumber = -1;
    devicePlan.cfg5711.cfg5711.channelCount = 0;
    devicePlan.cfg5711.cfg5711.slotNumber = -1;
    devicePlan.cfg8902.cfg8902.sampleCount = 0;
    devicePlan.cfg8902.cfg8902.slotNumber = -1;

    *plan = devicePlan;
    return true;
}

bool ExampleTpsPlugin::configure(const QMap<QString, QVariant> &settings, QString *error)
{
    Q_UNUSED(error)
    m_settings = settings;
    return true;
}

bool ExampleTpsPlugin::execute(const TPSRequest &request, TPSResult *result, QString *error)
{
    if (!result) {
        if (error) {
            *error = QStringLiteral("result is null");
        }
        return false;
    }

    result->runId = request.runId;
    result->success = true;
    result->summary = QStringLiteral("executed %1 UTR items").arg(request.items.size());
    result->metrics.insert(QStringLiteral("items"), request.items.size());
    result->metrics.insert(QStringLiteral("settingsCount"), m_settings.size());
    return true;
}
