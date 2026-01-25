#include "deviceportplanner.h"

DevicePortPlanner::Request DevicePortPlanner::buildDefaultRequest()
{
    Request request;
    request.needResistance = false;
    request.capture5322Ports = 2;
    request.capture5323Ports = 2;

    OutputRequirement currentOut1;
    currentOut1.mode = OutputMode::Current;
    currentOut1.amplitude = 0.02;
    currentOut1.frequency = 0.0;
    currentOut1.duty = 0.5;
    request.outputs5711.push_back(currentOut1);

    OutputRequirement currentOut2;
    currentOut2.mode = OutputMode::Current;
    currentOut2.amplitude = 0.01;
    currentOut2.frequency = 0.0;
    currentOut2.duty = 0.5;
    request.outputs5711.push_back(currentOut2);

    OutputRequirement voltageOut1;
    voltageOut1.mode = OutputMode::Voltage;
    voltageOut1.amplitude = 5.0;
    voltageOut1.frequency = 0.0;
    voltageOut1.duty = 0.5;
    request.outputs5711.push_back(voltageOut1);

    OutputRequirement voltageOut2;
    voltageOut2.mode = OutputMode::Voltage;
    voltageOut2.amplitude = 3.3;
    voltageOut2.frequency = 0.0;
    voltageOut2.duty = 0.5;
    request.outputs5711.push_back(voltageOut2);

    return request;
}
