#ifndef CONNECTORDIAGNOSTICPLUGIN_H
#define CONNECTORDIAGNOSTICPLUGIN_H

#include <QObject>

#include "../diagnosticplugininterface.h"

class ConnectorDiagnosticPlugin : public QObject, public DiagnosticPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DiagnosticPluginInterface)

public:
    explicit ConnectorDiagnosticPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QString componentType() const override;

    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const override;
};

#endif // CONNECTORDIAGNOSTICPLUGIN_H
