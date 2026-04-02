#ifndef FUSEDIAGNOSTICPLUGIN_H
#define FUSEDIAGNOSTICPLUGIN_H

#include <QObject>

#include "../diagnosticplugininterface.h"

class FuseDiagnosticPlugin : public QObject, public DiagnosticPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(DiagnosticPluginInterface)

public:
    explicit FuseDiagnosticPlugin(QObject *parent = nullptr);

    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;
    QString componentType() const override;

    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const override;
};

#endif // FUSEDIAGNOSTICPLUGIN_H
