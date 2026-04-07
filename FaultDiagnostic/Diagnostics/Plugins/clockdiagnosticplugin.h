#ifndef CLOCKDIAGNOSTICPLUGIN_H
#define CLOCKDIAGNOSTICPLUGIN_H

#include <QObject>

#include "../diagnosticplugininterface.h"

// 时钟元件的诊断插件框架，负责消费采样数据并输出故障报告。
class ClockDiagnosticPlugin : public QObject, public DiagnosticPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DiagnosticPluginInterface)

public:
    explicit ClockDiagnosticPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QString componentType() const override;

    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const override;
};

#endif // CLOCKDIAGNOSTICPLUGIN_H
