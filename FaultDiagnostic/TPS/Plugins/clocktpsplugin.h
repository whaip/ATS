#ifndef CLOCKTPSPLUGIN_H
#define CLOCKTPSPLUGIN_H

#include <QObject>

#include "../Core/tpsplugininterface.h"

class ClockTpsPlugin : public QObject, public TPSPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(TPSPluginInterface)

public:
    explicit ClockTpsPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QVector<TPSParamDefinition> parameterDefinitions() const override;
    TPSPluginRequirement requirements() const override;
    bool buildDevicePlan(const QVector<TPSPortBinding> &bindings,
                         const QMap<QString, QVariant> &settings,
                         TPSDevicePlan *plan,
                         QString *error) override;
    bool configure(const QMap<QString, QVariant> &settings, QString *error) override;
    bool execute(const TPSRequest &request, TPSResult *result, QString *error) override;

private:
    QMap<QString, QVariant> m_settings;
};

#endif // CLOCKTPSPLUGIN_H
