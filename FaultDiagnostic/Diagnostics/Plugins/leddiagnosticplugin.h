#ifndef LEDDIAGNOSTICPLUGIN_H
#define LEDDIAGNOSTICPLUGIN_H

#include <QObject>

#include "../diagnosticplugininterface.h"

class LedDiagnosticPlugin : public QObject, public DiagnosticPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DiagnosticPluginInterface)

public:
    explicit LedDiagnosticPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QString componentType() const override;

    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const override;
};

#endif // LEDDIAGNOSTICPLUGIN_H
