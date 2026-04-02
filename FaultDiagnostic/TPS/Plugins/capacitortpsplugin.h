#ifndef CAPACITORTPSPLUGIN_H
#define CAPACITORTPSPLUGIN_H

#include <QObject>

#include "../Core/tpsplugininterface.h"

class CapacitorTpsPlugin : public QObject, public TPSPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(TPSPluginInterface)

public:
    explicit CapacitorTpsPlugin(QObject *parent = nullptr);

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
    int expectedMode() const;
    static const TPSPortBinding *findBinding(const QVector<TPSPortBinding> &bindings, const QString &identifier);

    QMap<QString, QVariant> m_settings;
    QVector<TPSPortBinding> m_allocatedBindings;
    JYDeviceConfig m_config5711;
    bool m_configReady = false;
};

#endif // CAPACITORTPSPLUGIN_H
