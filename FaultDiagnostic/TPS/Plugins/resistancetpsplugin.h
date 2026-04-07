#ifndef RESISTANCETPSPLUGIN_H
#define RESISTANCETPSPLUGIN_H

#include <QObject>

#include "../Core/tpsplugininterface.h"

// 电阻元件的 TPS 插件，实现测量策略、端口需求和设备计划生成。
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
    QMap<QString, QVariant> m_settings;
    QVector<TPSPortBinding> m_allocatedBindings;
    JYDeviceConfig m_config5711;
    bool m_configReady = false;
};

#endif // RESISTANCETPSPLUGIN_H
