#include "multitpsplugin.h"

MultiSignalTpsPlugin::MultiSignalTpsPlugin(QObject *parent)
    : QObject(parent)
{
}

QString MultiSignalTpsPlugin::pluginId() const
{
    return QStringLiteral("tps.multi.signal");
}

QString MultiSignalTpsPlugin::displayName() const
{
    return QStringLiteral("Multi Signal TPS");
}

QString MultiSignalTpsPlugin::version() const
{
    return QStringLiteral("1.0.0");
}

QVector<TPSParamDefinition> MultiSignalTpsPlugin::parameterDefinitions() const
{
    TPSParamDefinition channels;
    channels.key = QStringLiteral("channelCount");
    channels.label = QStringLiteral("通道数");
    channels.type = TPSParamType::Integer;
    channels.defaultValue = 2;
    channels.minValue = 1;
    channels.maxValue = 16;
    channels.stepValue = 1;

    TPSParamDefinition mode;
    mode.key = QStringLiteral("mode");
    mode.label = QStringLiteral("采集模式");
    mode.type = TPSParamType::Enum;
    mode.enumOptions = {QStringLiteral("并行"), QStringLiteral("顺序")};
    mode.defaultValue = mode.enumOptions.value(0);

    return {channels, mode};
}

bool MultiSignalTpsPlugin::configure(const QMap<QString, QVariant> &settings, QString *error)
{
    Q_UNUSED(error)
    m_settings = settings;
    return true;
}

bool MultiSignalTpsPlugin::execute(const TPSRequest &request, TPSResult *result, QString *error)
{
    if (!result) {
        if (error) {
            *error = QStringLiteral("result is null");
        }
        return false;
    }

    result->runId = request.runId;
    result->success = true;
    result->summary = QStringLiteral("executed %1 items").arg(request.items.size());
    result->metrics.insert(QStringLiteral("items"), request.items.size());
    result->metrics.insert(QStringLiteral("settingsCount"), m_settings.size());
    return true;
}
