#include "tpsbuiltinregistry.h"

#include "tpspluginmanager.h"
#include "../Plugins/exampletpsplugin.h"
#include "../Plugins/multitpsplugin.h"
#include "../Plugins/resistancetpsplugin.h"
#include "../Plugins/capacitortpsplugin.h"
#include "../Plugins/inductortpsplugin.h"
#include "../Plugins/transistortpsplugin.h"
#include "../Plugins/typicaltpsplugin.h"

void registerDefaultTpsBuiltins(TPSPluginManager *manager, QObject *parentForPlugins)
{
    if (!manager) {
        return;
    }

    auto *example = new ExampleTpsPlugin();
    if (parentForPlugins) {
        example->setParent(parentForPlugins);
    }
    manager->addBuiltin(example);

    auto *resistance = new ResistanceTpsPlugin(parentForPlugins);
    manager->addBuiltin(resistance);

    auto *capacitor = new CapacitorTpsPlugin(parentForPlugins);
    manager->addBuiltin(capacitor);

    auto *inductor = new InductorTpsPlugin(parentForPlugins);
    manager->addBuiltin(inductor);

    auto *transistor = new TransistorTpsPlugin(parentForPlugins);
    manager->addBuiltin(transistor);

    auto *multi = new MultiSignalTpsPlugin(parentForPlugins);
    manager->addBuiltin(multi);

    auto *typical = new TypicalTpsPlugin(parentForPlugins);
    manager->addBuiltin(typical);
}
