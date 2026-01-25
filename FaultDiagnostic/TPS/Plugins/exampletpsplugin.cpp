#include "exampletpsplugin.h"

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

    return {note, retry};
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
