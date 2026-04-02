#ifndef ICDIAGNOSTICPLUGIN_H
#define ICDIAGNOSTICPLUGIN_H

#include <QObject>

#include "../diagnosticplugininterface.h"

class ICDiagnosticPlugin : public QObject, public DiagnosticPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DiagnosticPluginInterface)

public:
    explicit ICDiagnosticPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QString componentType() const override;

    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const override;
};

#endif // ICDIAGNOSTICPLUGIN_H
