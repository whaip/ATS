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

// 统一的设备生命周期状态，供 worker / orchestrator / UI 共同使用。
enum class JYDeviceState {
    Closed,
    Configured,
    Armed,
    Running,
    Faulted
};

struct JY532xConfig {
    // PXIe-5322/PXIe-5323 在机箱中的槽位号.
    int slotNumber = 0;
    // 使能的通道数.
    int channelCount = 16;
    // 采样率（Hz）.
    double sampleRate = 1000000.0;
    // 每次读取的样本数.
    int samplesPerRead = 1024;
    // 超时时间（ms）.
    int timeoutMs = 1000;
    double lowRange = -10.0;
    double highRange = 10.0;
    int bandwidth = 0;
};

struct JY5711WaveformConfig {
    int channel = 0;
    QString waveformId = PXIe5711_default_waveform_id();
    QMap<QString, double> params = PXIe5711_default_param_map(PXIe5711_default_waveform_id());

    // 校验波形 ID 和参数表，确保配置可以直接用于创建波形对象。
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
    // 便捷构造函数：外部通常通过它生成某一路的波形配置。
    JY5711WaveformConfig config;
    config.channel = channel;
    config.waveformId = waveformId;
    config.params = PXIe5711_merge_params(waveformId, params);
    return config;
}

struct JY5711Config {
    int slotNumber = 0;
    int channelCount = 1;
    // Output update rate in Hz.
    double sampleRate = 1000000.0;
    double lowRange = -10.0;
    double highRange = 10.0;
    QVector<int> enabledChannels;
    QVector<JY5711WaveformConfig> waveforms;
};

struct JY8902Config {
    int slotNumber = 0;
    // Number of points produced by one DMM multi-point read.
    int sampleCount = 20;
    int timeoutMs = 1000;
    int measurementFunction = 0;
    int range = -1;
    // Integration/aperture time of one point in seconds.
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
    // Number of interleaved channels in data.
    int channelCount = 0;
    // Number of samples for each channel in this packet.
    int samplesPerChannel = 0;
    // Effective sample rate in Hz for one channel.
    double sampleRateHz = 0.0;
    // Global sample index of the first sample in this packet.
    quint64 startSampleIndex = 0;
    // Interleaved samples laid out as [ch0, ch1, ..., chN, ch0, ...].
    QVector<double> data;
    // Packet end timestamp in milliseconds.
    qint64 timestampMs = 0;
};

struct JYAlignedBatch {
    // One aligned packet per device kind.
    QMap<JYDeviceKind, JYDataPacket> packets;
    // Alignment window start timestamp in milliseconds.
    qint64 timestampMs = 0;
};

#endif // JYDEVICETYPE_H
