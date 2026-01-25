#ifndef JYDEVICECONFIGUTILS_H
#define JYDEVICECONFIGUTILS_H

#include "jydevicetype.h"

QString jyDeviceStateText(JYDeviceState state, const QString &message);
JYDeviceConfig build532xInitConfig(JYDeviceKind kind);
JYDeviceConfig build5711InitConfig();
JYDeviceConfig build8902InitConfig();

#endif // JYDEVICECONFIGUTILS_H
