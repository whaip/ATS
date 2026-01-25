# TPS Plugin Module

This folder provides a lightweight Qt plugin framework for TPS (Test Program Set) development.

## Files
- `tpsmodels.h`: UTR request/result data models.
- `tpsplugininterface.h`: TPS plugin interface contract.
- `tpspluginmanager.*`: Plugin loader and registry.
- `exampletpsplugin.*`: Minimal example plugin.

## How it works
TPS plugins are compiled as dynamic libraries and loaded via `QPluginLoader` from the `tps_plugins` directory (next to the app binary) unless overridden with `TPSPluginManager::setPluginDir`.

## Build note
Example plugin is included in the main build by default in this repo. In production, you should build plugins into separate DLLs and drop them into the `tps_plugins` folder.
