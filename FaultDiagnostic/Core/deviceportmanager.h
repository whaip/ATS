#ifndef DEVICEPORTMANAGER_H
#define DEVICEPORTMANAGER_H

#include <QVector>
#include <QString>

#include "../TPS/Core/tpsmodels.h"

class DevicePortManager
{
public:
    static bool allocate(const QVector<TPSPortRequest> &requests,
                         QVector<TPSPortBinding> *bindings,
                         QString *error);
};

#endif // DEVICEPORTMANAGER_H
