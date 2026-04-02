#ifndef JYDEVICECONFIGUTILS_H
#define JYDEVICECONFIGUTILS_H

#include "jydevicetype.h"

// 将内部设备状态和驱动返回消息转换为界面可直接显示的文本。
QString jyDeviceStateText(JYDeviceState state, const QString &message);

// 构造 PXIe-5322 / PXIe-5323 的默认初始化配置。
JYDeviceConfig build532xInitConfig(JYDeviceKind kind);

// 构造 PXIe-5711 的默认初始化配置。
JYDeviceConfig build5711InitConfig();

// 构造 PXIe-8902 的默认初始化配置。
JYDeviceConfig build8902InitConfig();

#endif // JYDEVICECONFIGUTILS_H
