#include "diagnosticdatamapper.h"

namespace {
QVector<double> extractChannel(const JYDataPacket &packet, int channel)
{
    QVector<double> samples;
    if (packet.channelCount <= 0 || channel < 0 || channel >= packet.channelCount) {
        return samples;
    }
    const int samplesPerChannel = packet.samplesPerChannel > 0
        ? packet.samplesPerChannel
        : (packet.data.size() / packet.channelCount);
    if (samplesPerChannel <= 0) {
        return samples;
    }
    samples.reserve(samplesPerChannel);
    for (int i = 0; i < samplesPerChannel; ++i) {
        const int index = i * packet.channelCount + channel;
        if (index >= 0 && index < packet.data.size()) {
            samples.push_back(packet.data[index]);
        }
    }
    return samples;
}
}

QMap<QString, DiagnosticSignalSeries> DiagnosticDataMapper::mapSignals(const JYAlignedBatch &batch,
                                                                       const QVector<TPSPortBinding> &bindings)
{
    QMap<QString, DiagnosticSignalSeries> seriesMap;
    for (const auto &binding : bindings) {
        if (!batch.packets.contains(binding.deviceKind)) {
            continue;
        }
        const auto &packet = batch.packets.value(binding.deviceKind);
        DiagnosticSignalSeries series;
        series.samples = extractChannel(packet, binding.channel);
        series.sampleRateHz = packet.sampleRateHz;
        seriesMap.insert(binding.identifier, series);
    }
    return seriesMap;
}
