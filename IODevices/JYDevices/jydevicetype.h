#ifndef JYDEVICETYPE_H
#define JYDEVICETYPE_H

#include <QMap>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "5711waveformconfig.h"

enum class JYDeviceKind {
    PXIe5322,
    PXIe5323,
    PXIe5711,
    PXIe8902
};

enum class JYDeviceState {
    Closed,
    Configured,
    Armed,
    Running,
    Faulted
};

struct JY532xConfig {
    int slotNumber = 0;
    int channelCount = 16;
    double sampleRate = 1000000.0;
    int samplesPerRead = 1024;
    int timeoutMs = 1000;
    double lowRange = -10.0;
    double highRange = 10.0;
    int bandwidth = 0;
};

struct JY5711WaveformConfig {
    int channel = 0;
    QString waveformId = PXIe5711_default_waveform_id();
    QMap<QString, double> params = PXIe5711_default_param_map(PXIe5711_default_waveform_id());

    void ensureValid()
    {
        if (!PXIe5711_find_waveform(waveformId)) {
            waveformId = PXIe5711_default_waveform_id();
        }
        params = PXIe5711_merge_params(waveformId, params);
    }
};

inline JY5711WaveformConfig build5711WaveformConfig(int channel,
                                                    const QString &waveformId,
                                                    const QMap<QString, double> &params = {})
{
    JY5711WaveformConfig config;
    config.channel = channel;
    config.waveformId = waveformId;
    config.params = PXIe5711_merge_params(waveformId, params);
    return config;
}

struct JY5711Config {
    int slotNumber = 0;
    int channelCount = 1;
    double sampleRate = 1000000.0;
    double lowRange = -10.0;
    double highRange = 10.0;
    QVector<int> enabledChannels;
    QVector<JY5711WaveformConfig> waveforms;
};

struct JY8902Config {
    int slotNumber = 0;
    int sampleCount = 20;
    int timeoutMs = 1000;
    int measurementFunction = 0;
    int range = -1;
    double apertureTime = 0.02;
    double triggerDelay = 0.1;
};

struct JYDeviceConfig {
    JYDeviceKind kind = JYDeviceKind::PXIe5322;
    JY532xConfig cfg532x;
    JY5711Config cfg5711;
    JY8902Config cfg8902;
};

struct JYDataPacket {
    JYDeviceKind kind = JYDeviceKind::PXIe5322;
    int channelCount = 0;
    int samplesPerChannel = 0;
    double sampleRateHz = 0.0;
    quint64 startSampleIndex = 0;
    QVector<double> data;
    qint64 timestampMs = 0;
};

struct JYAlignedBatch {
    QMap<JYDeviceKind, JYDataPacket> packets;
    qint64 timestampMs = 0;
};

#endif // JYDEVICETYPE_H
