#ifndef EXAMPLEDIAGNOSTICPLUGIN_H
#define EXAMPLEDIAGNOSTICPLUGIN_H

#include <QObject>

#include "../diagnosticplugininterface.h"

// 示例诊断插件，用于演示波形一致性和温度阈值的诊断方式。
class ExampleDiagnosticPlugin : public QObject, public DiagnosticPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DiagnosticPluginInterface)

public:
    explicit ExampleDiagnosticPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QString componentType() const override;

    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const override;
};

#endif // EXAMPLEDIAGNOSTICPLUGIN_H
