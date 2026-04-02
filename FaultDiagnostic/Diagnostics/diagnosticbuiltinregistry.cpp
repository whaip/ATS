#include "diagnosticbuiltinregistry.h"

#include "Plugins/batterydiagnosticplugin.h"
#include "Plugins/buzzerdiagnosticplugin.h"
#include "Plugins/clockdiagnosticplugin.h"
#include "Plugins/connectordiagnosticplugin.h"
#include "diagnosticpluginmanager.h"
#include "Plugins/capacitordiagnosticplugin.h"
#include "Plugins/diodediagnosticplugin.h"
#include "Plugins/displaydiagnosticplugin.h"
#include "Plugins/examplediagnosticplugin.h"
#include "Plugins/fusediagnosticplugin.h"
#include "Plugins/icdiagnosticplugin.h"
#include "Plugins/inductordiagnosticplugin.h"
#include "Plugins/multitpsdiagnosticplugin.h"
#include "Plugins/leddiagnosticplugin.h"
#include "Plugins/potentiometerdiagnosticplugin.h"
#include "Plugins/relaydiagnosticplugin.h"
#include "Plugins/resistordiagnosticplugin.h"
#include "Plugins/switchdiagnosticplugin.h"
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
    manager->addBuiltin(new ICDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new LedDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new BatteryDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new BuzzerDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new ClockDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new ConnectorDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new DiodeDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new DisplayDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new FuseDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new PotentiometerDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new RelayDiagnosticPlugin(parentForPlugins));
    manager->addBuiltin(new SwitchDiagnosticPlugin(parentForPlugins));
}
