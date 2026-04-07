#ifndef EXAMPLETPSPLUGIN_H
#define EXAMPLETPSPLUGIN_H

#include <QObject>

#include "../Core/tpsplugininterface.h"

// 示例 TPS 插件，用于演示输出、采集、温度阈值等完整策略链路。
class ExampleTpsPlugin : public QObject, public TPSPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(TPSPluginInterface)

public:
    explicit ExampleTpsPlugin(QObject *parent = nullptr);

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
    static QString deviceKindName(JYDeviceKind kind);
    static QString portText(const TPSPortBinding &binding);
    static QString anchorText(const QMap<QString, QVariant> &settings, const QString &key);

    QMap<QString, QVariant> m_settings;
};

#endif // EXAMPLETPSPLUGIN_H
