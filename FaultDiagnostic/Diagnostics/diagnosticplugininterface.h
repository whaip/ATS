#ifndef DIAGNOSTICPLUGININTERFACE_H
#define DIAGNOSTICPLUGININTERFACE_H

#include <QObject>
#include <QString>

#include "diagnosticdatatypes.h"

class DiagnosticPluginInterface
{
public:
    virtual ~DiagnosticPluginInterface() = default;

    virtual QString pluginId() const = 0;
    virtual QString displayName() const = 0;
    virtual QString version() const = 0;
    virtual QString componentType() const = 0;

    virtual DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const = 0;
};

#define DiagnosticPluginInterface_iid "com.faultdetect.diagnosticplugin/1.0"
Q_DECLARE_INTERFACE(DiagnosticPluginInterface, DiagnosticPluginInterface_iid)

#endif // DIAGNOSTICPLUGININTERFACE_H
