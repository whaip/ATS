#ifndef DSHOWCAMERAUTIL_H
#define DSHOWCAMERAUTIL_H

#include <QList>
#include <QSize>
#include <QString>

namespace DShowCameraUtil {

// 一个分辨率/像素格式能力项，来自 DirectShow 能力枚举结果。
struct StreamCapability {
    QSize size;
    int fourcc = 0;          // OpenCV fourcc int (e.g. 'MJPG') when available, else 0
    QString formatName;      // Human readable, e.g. "MJPG", "YUY2"
    double minFps = 0.0;     // 0 means unknown
    double maxFps = 0.0;     // 0 means unknown
};

// 一个视频输入设备及其全部能力列表。
struct DeviceInfo {
    QString friendlyName;
    // Optional stable identifier (may be empty)
    QString devicePath;
    QList<StreamCapability> capabilities;
};

// 枚举 DirectShow 视频输入设备及其能力。
// 这里只读取驱动声明的能力，不通过实际切换格式来试探设备。
QList<DeviceInfo> enumerateDevices();

} // namespace DShowCameraUtil

#endif // DSHOWCAMERAUTIL_H
