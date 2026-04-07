#ifndef TRANSISTORDIAGNOSTICPLUGIN_H
#define TRANSISTORDIAGNOSTICPLUGIN_H

#include <QObject>

#include "../diagnosticplugininterface.h"

// 三极管元件的诊断插件，实现对采样波形的特征提取和故障判定。
class TransistorDiagnosticPlugin : public QObject, public DiagnosticPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DiagnosticPluginInterface)

public:
    explicit TransistorDiagnosticPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QString componentType() const override;

    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const override;
};

#endif // TRANSISTORDIAGNOSTICPLUGIN_H
