#ifndef MULTITPSDIAGNOSTICPLUGIN_H
#define MULTITPSDIAGNOSTICPLUGIN_H

#include <QObject>

#include "../diagnosticplugininterface.h"

// 多信号示例诊断插件，用于对组合采样结果做统一判定。
class MultiTpsDiagnosticPlugin : public QObject, public DiagnosticPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DiagnosticPluginInterface)

public:
    explicit MultiTpsDiagnosticPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QString componentType() const override;

    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const override;
};

#endif // MULTITPSDIAGNOSTICPLUGIN_H
