#ifndef DSHOWCAMERAUTIL_H
#define DSHOWCAMERAUTIL_H

#include <QList>
#include <QSize>
#include <QString>

namespace DShowCameraUtil {

struct StreamCapability {
    QSize size;
    int fourcc = 0;          // OpenCV fourcc int (e.g. 'MJPG') when available, else 0
    QString formatName;      // Human readable, e.g. "MJPG", "YUY2"
    double minFps = 0.0;     // 0 means unknown
    double maxFps = 0.0;     // 0 means unknown
};

struct DeviceInfo {
    QString friendlyName;
    // Optional stable identifier (may be empty)
    QString devicePath;
    QList<StreamCapability> capabilities;
};

// Enumerates DirectShow video input devices and their supported resolutions.
// This does NOT probe by setting formats; it queries IAMStreamConfig capabilities.
QList<DeviceInfo> enumerateDevices();

} // namespace DShowCameraUtil

#endif // DSHOWCAMERAUTIL_H
