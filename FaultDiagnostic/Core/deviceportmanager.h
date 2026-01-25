#ifndef DEVICEPORTMANAGER_H
#define DEVICEPORTMANAGER_H

#include <QVector>
#include <QString>

#include "deviceportplanner.h"
#include "../Runtime/systemorchestration.h"
#include "../../IODevices/JYDevices/jydevicetype.h"

class DevicePortManager
{
public:
    struct Allocation {
        QVector<ResourceMapping> mappings;
        QVector<SignalRequest> requests;
        JYDeviceConfig cfg532x;
        JYDeviceConfig cfg5711;
        JYDeviceConfig cfg8902;
    };

    static Allocation allocate(const DevicePortPlanner::Request &request);
};

#endif // DEVICEPORTMANAGER_H
