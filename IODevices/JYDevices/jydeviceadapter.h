#ifndef JYDEVICEADAPTER_H
#define JYDEVICEADAPTER_H

#include "jydevicetype.h"

#include <QString>
#include <memory>

class JYDeviceAdapter
{
public:
    virtual ~JYDeviceAdapter() = default;

    // 返回当前适配器对应的物理设备类型。
    virtual JYDeviceKind kind() const = 0;

    // 打开底层设备并应用配置。
    virtual bool configure(const JYDeviceConfig &config, QString *error) = 0;

    // 让设备进入已启动/已布防状态，但不一定已经真正开始采样。
    virtual bool start(QString *error) = 0;

    // 发出软触发，让采集或输出真正开始。
    virtual bool trigger(QString *error) = 0;

    // 停止采集或输出，但保持设备句柄仍然打开。
    virtual bool stop(QString *error) = 0;

    // 关闭设备并释放驱动资源。
    virtual bool close(QString *error) = 0;

    // 读取一个数据包；对纯输出设备可实现为空操作。
    virtual bool read(JYDataPacket *out, QString *error) = 0;
};

// 创建 PXIe-5322 / PXIe-5323 输入板适配器。
std::unique_ptr<JYDeviceAdapter> createJY532xAdapter(JYDeviceKind kind);
// 创建 PXIe-5711 输出板适配器。
std::unique_ptr<JYDeviceAdapter> createJY5711Adapter();
// 创建 PXIe-8902 万用表适配器。
std::unique_ptr<JYDeviceAdapter> createJY8902Adapter();

#endif // JYDEVICEADAPTER_H
