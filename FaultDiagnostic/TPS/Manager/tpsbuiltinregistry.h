#ifndef TPSBUILTINREGISTRY_H
#define TPSBUILTINREGISTRY_H

class TPSPluginManager;
class QObject;

void registerDefaultTpsBuiltins(TPSPluginManager *manager, QObject *parentForPlugins);

#endif // TPSBUILTINREGISTRY_H
