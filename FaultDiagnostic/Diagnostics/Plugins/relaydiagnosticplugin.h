#ifndef RELAYDIAGNOSTICPLUGIN_H
#define RELAYDIAGNOSTICPLUGIN_H

#include <QObject>

#include "../diagnosticplugininterface.h"

// 继电器元件的诊断插件框架，负责消费采样数据并输出故障报告。
class RelayDiagnosticPlugin : public QObject, public DiagnosticPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DiagnosticPluginInterface)

public:
    explicit RelayDiagnosticPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QString componentType() const override;

    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const override;
};

#endif // RELAYDIAGNOSTICPLUGIN_H
