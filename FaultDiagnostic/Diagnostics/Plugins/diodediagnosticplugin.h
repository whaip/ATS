#ifndef DIODEDIAGNOSTICPLUGIN_H
#define DIODEDIAGNOSTICPLUGIN_H

#include <QObject>

#include "../diagnosticplugininterface.h"

class DiodeDiagnosticPlugin : public QObject, public DiagnosticPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DiagnosticPluginInterface)

public:
    explicit DiodeDiagnosticPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QString componentType() const override;

    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const override;
};

#endif // DIODEDIAGNOSTICPLUGIN_H
