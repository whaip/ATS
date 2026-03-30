#ifndef DIAGNOSTICBUILTINREGISTRY_H
#define DIAGNOSTICBUILTINREGISTRY_H

class DiagnosticPluginManager;
class QObject;

void registerDefaultDiagnosticBuiltins(DiagnosticPluginManager *manager, QObject *parentForPlugins);

#endif // DIAGNOSTICBUILTINREGISTRY_H
