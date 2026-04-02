#include "tpsbuiltinregistry.h"

#include "tpspluginmanager.h"
#include "../Plugins/batterytpsplugin.h"
#include "../Plugins/buzzertpsplugin.h"
#include "../Plugins/exampletpsplugin.h"
#include "../Plugins/capacitortpsplugin.h"
#include "../Plugins/clocktpsplugin.h"
#include "../Plugins/connectortpsplugin.h"
#include "../Plugins/diodetpsplugin.h"
#include "../Plugins/displaytpsplugin.h"
#include "../Plugins/fusetpsplugin.h"
#include "../Plugins/ictpsplugin.h"
#include "../Plugins/inductortpsplugin.h"
#include "../Plugins/ledtpsplugin.h"
#include "../Plugins/multitpsplugin.h"
#include "../Plugins/potentiometertpsplugin.h"
#include "../Plugins/relaytpsplugin.h"
#include "../Plugins/resistancetpsplugin.h"
#include "../Plugins/switchtpsplugin.h"
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
    manager->addBuiltin(new ICTpsPlugin(parentForPlugins));
    manager->addBuiltin(new LedTpsPlugin(parentForPlugins));
    manager->addBuiltin(new BatteryTpsPlugin(parentForPlugins));
    manager->addBuiltin(new BuzzerTpsPlugin(parentForPlugins));
    manager->addBuiltin(new ClockTpsPlugin(parentForPlugins));
    manager->addBuiltin(new ConnectorTpsPlugin(parentForPlugins));
    manager->addBuiltin(new DiodeTpsPlugin(parentForPlugins));
    manager->addBuiltin(new DisplayTpsPlugin(parentForPlugins));
    manager->addBuiltin(new FuseTpsPlugin(parentForPlugins));
    manager->addBuiltin(new PotentiometerTpsPlugin(parentForPlugins));
    manager->addBuiltin(new RelayTpsPlugin(parentForPlugins));
    manager->addBuiltin(new SwitchTpsPlugin(parentForPlugins));
}
