#ifndef CAPTUREDDATAMANAGER_H
#define CAPTUREDDATAMANAGER_H

#include <QMap>
#include <QVector>

#include "../TPS/Core/tpsmodels.h"
#include "../../IODevices/JYDevices/jydevicetype.h"
#include "../Diagnostics/diagnosticdatatypes.h"

class CapturedDataManager
{
public:
    void reset();
    void appendPacket(const JYDataPacket &packet);

    bool buildSeries(JYDeviceKind kind, int channel, QVector<double> *times, QVector<double> *values) const;
    QMap<QString, DiagnosticSignalSeries> buildSignalSeries(const QVector<TPSPortBinding> &bindings) const;
    int totalSamples(JYDeviceKind kind) const;

private:
    struct ChannelSeries {
        QVector<double> samples;
    };

    struct DeviceSeries {
        int channelCount = 0;
        double sampleRateHz = 0.0;
        QMap<int, ChannelSeries> channels;
    };

    QMap<JYDeviceKind, DeviceSeries> m_devices;
};

#endif // CAPTUREDDATAMANAGER_H
