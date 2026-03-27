#ifndef RESISTANCETPSPLUGIN_H
#define RESISTANCETPSPLUGIN_H

#include <QObject>

#include "../Core/tpsplugininterface.h"

class ResistanceTpsPlugin : public QObject, public TPSPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(TPSPluginInterface)

public:
    explicit ResistanceTpsPlugin(QObject *parent = nullptr);
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
    bool collectInputSamples(const TPSRequest &request, QVector<double> *samples) const;
    double resolveExcitationVoltage(const TPSRequest &request) const;

    QMap<QString, QVariant> m_settings;
    QVector<TPSPortBinding> m_allocatedBindings;
    JYDeviceConfig m_config5711;
    bool m_configReady = false;
};

#endif // RESISTANCETPSPLUGIN_H
