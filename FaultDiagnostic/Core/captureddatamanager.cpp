#include "captureddatamanager.h"

void CapturedDataManager::reset()
{
    m_devices.clear();
}

void CapturedDataManager::appendPacket(const JYDataPacket &packet)
{
    if (packet.channelCount <= 0 || packet.data.isEmpty()) {
        return;
    }

    DeviceSeries &device = m_devices[packet.kind];
    device.channelCount = qMax(device.channelCount, packet.channelCount);
    if (packet.sampleRateHz > 0.0) {
        device.sampleRateHz = packet.sampleRateHz;
    }

    const int samplesPerChannel = packet.samplesPerChannel > 0
        ? packet.samplesPerChannel
        : (packet.data.size() / packet.channelCount);
    if (samplesPerChannel <= 0) {
        return;
    }

    for (int ch = 0; ch < packet.channelCount; ++ch) {
        ChannelSeries &series = device.channels[ch];
        series.samples.reserve(series.samples.size() + samplesPerChannel);
    }

    for (int i = 0; i < samplesPerChannel; ++i) {
        for (int ch = 0; ch < packet.channelCount; ++ch) {
            const int idx = i * packet.channelCount + ch;
            if (idx >= 0 && idx < packet.data.size()) {
                device.channels[ch].samples.push_back(packet.data[idx]);
            }
        }
    }
}

bool CapturedDataManager::buildSeries(JYDeviceKind kind, int channel, QVector<double> *times, QVector<double> *values) const
{
    if (!times || !values) {
        return false;
    }

    const auto it = m_devices.find(kind);
    if (it == m_devices.end()) {
        return false;
    }

    const DeviceSeries &device = it.value();
    const auto chIt = device.channels.find(channel);
    if (chIt == device.channels.end()) {
        return false;
    }

    const QVector<double> &samples = chIt.value().samples;
    if (samples.isEmpty()) {
        return false;
    }

    times->clear();
    values->clear();
    times->reserve(samples.size());
    values->reserve(samples.size());

    const double rate = device.sampleRateHz > 0.0 ? device.sampleRateHz : 1.0;
    const double dt = 1.0 / rate;
    for (int i = 0; i < samples.size(); ++i) {
        times->push_back(i * dt);
        values->push_back(samples[i]);
    }
    return true;
}

QMap<QString, DiagnosticSignalSeries> CapturedDataManager::buildSignalSeries(const QVector<TPSPortBinding> &bindings) const
{
    QMap<QString, DiagnosticSignalSeries> result;
    for (const auto &binding : bindings) {
        const auto it = m_devices.find(binding.deviceKind);
        if (it == m_devices.end()) {
            continue;
        }
        const DeviceSeries &device = it.value();
        const auto chIt = device.channels.find(binding.channel);
        if (chIt == device.channels.end()) {
            continue;
        }

        DiagnosticSignalSeries series;
        series.samples = chIt.value().samples;
        series.sampleRateHz = device.sampleRateHz;
        result.insert(binding.identifier, series);
    }
    return result;
}

int CapturedDataManager::totalSamples(JYDeviceKind kind) const
{
    const auto it = m_devices.find(kind);
    if (it == m_devices.end()) {
        return 0;
    }
    int maxSamples = 0;
    for (auto chIt = it.value().channels.begin(); chIt != it.value().channels.end(); ++chIt) {
        maxSamples = qMax(maxSamples, chIt.value().samples.size());
    }
    return maxSamples;
}
