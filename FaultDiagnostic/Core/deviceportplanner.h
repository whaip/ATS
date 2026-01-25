#ifndef DEVICEPORTPLANNER_H
#define DEVICEPORTPLANNER_H

#include <QVector>
#include <QString>

#include "../../IODevices/JYDevices/jydevicetype.h"
#include "../Runtime/systemorchestration.h"

class DevicePortPlanner
{
public:
    enum class OutputMode {
        Current,
        Voltage
    };

    struct OutputRequirement {
        OutputMode mode = OutputMode::Voltage;
        double amplitude = 0.0;
        double frequency = 0.0;
        double duty = 0.5;
    };

    struct Request {
        bool needResistance = true;
        int capture5322Ports = 1;
        int capture5323Ports = 1;
        QVector<OutputRequirement> outputs5711;
    };

    static Request buildDefaultRequest();
};

#endif // DEVICEPORTPLANNER_H
