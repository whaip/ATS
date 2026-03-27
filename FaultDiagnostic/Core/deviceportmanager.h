#ifndef DEVICEPORTMANAGER_H
#define DEVICEPORTMANAGER_H

#include <QVector>
#include <QString>

#include "../TPS/Core/tpsmodels.h"

class DevicePortManager
{
public:
    struct AllocationState {
        int nextCurrentOut = 0;
        int nextVoltageOut = 0;
        int nextCurrentIn = 0;
        int nextVoltageIn = 0;
        int nextDmm = 0;
        JYDeviceConfig cfg5711;
        JYDeviceConfig cfg5322;
        JYDeviceConfig cfg5323;
        JYDeviceConfig cfg8902;
    };

    static AllocationState defaultState();
    static bool allocate(const QVector<TPSPortRequest> &requests,
                         QVector<TPSPortBinding> *bindings,
                         QString *error);
    static bool allocate(const QVector<TPSPortRequest> &requests,
                         AllocationState *state,
                         QVector<TPSPortBinding> *bindings,
                         QString *error);

    // QList is an alias of QVector in Qt6; keep only QVector overloads.
};

#endif // DEVICEPORTMANAGER_H
