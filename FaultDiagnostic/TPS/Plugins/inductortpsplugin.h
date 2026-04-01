#ifndef INDUCTORTPSPLUGIN_H
#define INDUCTORTPSPLUGIN_H

#include <QObject>

#include "../Core/tpsplugininterface.h"

class InductorTpsPlugin : public QObject, public TPSPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(TPSPluginInterface)

public:
    explicit InductorTpsPlugin(QObject *parent = nullptr);

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
    static const TPSPortBinding *findBinding(const QVector<TPSPortBinding> &bindings, const QString &identifier);

    static QString portText(const TPSPortBinding &binding);
    static QString anchorText(const QMap<QString, QVariant> &settings, const QString &key);

    QMap<QString, QVariant> m_settings;
    QVector<TPSPortBinding> m_allocatedBindings;
    JYDeviceConfig m_config5711;
    bool m_configReady = false;
};

#endif // INDUCTORTPSPLUGIN_H
