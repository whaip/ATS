#include "diagnosticbuiltinregistry.h"

#include "diagnosticpluginmanager.h"
#include "Plugins/examplediagnosticplugin.h"
#include "Plugins/capacitordiagnosticplugin.h"
#include "Plugins/inductordiagnosticplugin.h"
#include "Plugins/multitpsdiagnosticplugin.h"
#include "Plugins/resistordiagnosticplugin.h"
#include "Plugins/transistordiagnosticplugin.h"
#include "Plugins/typicaldiagnosticplugin.h"

void registerDefaultDiagnosticBuiltins(DiagnosticPluginManager *manager, QObject *parentForPlugins)
{
    if (!manager) {
        return;
    }

    manager->addBuiltin(new ExampleDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new MultiTpsDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new CapacitorDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new InductorDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new ResistorDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new TypicalDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new TransistorDiagnosticPlugin(parentForPlugins));
}
