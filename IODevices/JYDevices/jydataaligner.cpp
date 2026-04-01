#include "jydataaligner.h"

#include <QtMath>

namespace {
double toSeconds(qint64 ms)
{
    return static_cast<double>(ms) / 1000.0;
}
}

JYDataAligner::JYDataAligner(QObject *parent)
    : QObject(parent)
{
}

void JYDataAligner::setSettings(const Settings &settings)
{
    m_settings = settings;
}

void JYDataAligner::setExpectedKinds(const QSet<JYDeviceKind> &kinds)
{
    m_expected = kinds;
}

void JYDataAligner::setSyncAnchorMs(qint64 anchorMs)
{
    if (anchorMs <= 0) {
        m_hasAnchor = false;
        m_anchorTimeSeconds = 0.0;
        return;
    }
    m_anchorTimeSeconds = toSeconds(anchorMs);
    m_hasAnchor = true;
}

void JYDataAligner::ingest(const JYDataPacket &packet)
{
    if (packet.sampleRateHz <= 0.0 || packet.samplesPerChannel <= 0) {
        emit packetDropped(packet.kind, QStringLiteral("invalid sampling metadata"));
        return;
    }

    if (m_expected.contains(packet.kind) || m_expected.isEmpty()) {
        m_latest[packet.kind] = packet;
    } else {
        emit packetDropped(packet.kind, QStringLiteral("unexpected device"));
        return;
    }

    qint64 newest = packet.timestampMs;
    purgeStale(newest);

    // 只有当期望设备集合全部到齐时，才允许输出一个 aligned batch。
    if (!m_expected.isEmpty()) {
        for (auto kind : m_expected) {
            if (!m_latest.contains(kind)) {
                return;
            }
        }
    }

    JYAlignedBatch batch;
    if (buildAlignedBatch(batch)) {
        emit alignedReady(batch);
        m_latest.clear();
    }
}

void JYDataAligner::purgeStale(qint64 newestTs)
{
    if (m_settings.maxAgeMs <= 0) {
        return;
    }
    const qint64 cutoff = newestTs - m_settings.maxAgeMs;
    auto it = m_latest.begin();
    while (it != m_latest.end()) {
        if (it->timestampMs < cutoff) {
            emit packetDropped(it->kind, QStringLiteral("stale"));
            it = m_latest.erase(it);
        } else {
            ++it;
        }
    }
}

bool JYDataAligner::buildAlignedBatch(JYAlignedBatch &batch)
{
    if (m_latest.isEmpty()) {
        return false;
    }

    // 以最高采样率作为参考时间轴，低采样率设备向这条时间轴插值对齐。
    double refRate = 0.0;
    for (auto it = m_latest.begin(); it != m_latest.end(); ++it) {
        refRate = qMax(refRate, it->sampleRateHz);
    }
    if (refRate <= 0.0) {
        return false;
    }

    double maxStart = -1e18;
    double minEnd = 1e18;

    for (auto it = m_latest.begin(); it != m_latest.end(); ++it) {
        const auto &pkt = it.value();
        const double rate = pkt.sampleRateHz;
        const double endIndex = static_cast<double>(pkt.startSampleIndex + pkt.samplesPerChannel - 1);
        const double endTime = toSeconds(pkt.timestampMs);
        const double startTime = endTime - (static_cast<double>(pkt.samplesPerChannel - 1) / rate);

        if (!m_hasAnchor) {
            m_anchorTimeSeconds = startTime - (static_cast<double>(pkt.startSampleIndex) / rate);
            m_hasAnchor = true;
        }

        // 每个包先根据 sampleIndex、sampleRate 和锚点恢复出绝对时间范围，
        // 再对所有设备的时间范围求交集。
        const double packetStart = m_anchorTimeSeconds + static_cast<double>(pkt.startSampleIndex) / rate;
        const double packetEnd = m_anchorTimeSeconds + endIndex / rate;

        maxStart = qMax(maxStart, packetStart);
        minEnd = qMin(minEnd, packetEnd);
    }

    if (minEnd <= maxStart) {
        return false;
    }

    double windowSeconds = (m_settings.windowMs > 0) ? (static_cast<double>(m_settings.windowMs) / 1000.0) : (minEnd - maxStart);
    const double targetEnd = qMin(minEnd, maxStart + windowSeconds);
    const double step = 1.0 / refRate;
    if (targetEnd - maxStart < step) {
        return false;
    }

    QVector<double> targetTimes;
    // 目标时间轴按 refRate 等间隔生成，长度由 windowMs 和公共交集共同决定。
    for (double t = maxStart; t <= targetEnd + 1e-12; t += step) {
        targetTimes.push_back(t);
    }

    batch.packets.clear();
    batch.timestampMs = qRound64(maxStart * 1000.0);

    for (auto it = m_latest.begin(); it != m_latest.end(); ++it) {
        const auto &pkt = it.value();
        const auto channels = deinterleave(pkt);

        QVector<QVector<double>> resampled;
        resampled.reserve(channels.size());
        for (const auto &ch : channels) {
            resampled.push_back(resampleChannel(ch, pkt.sampleRateHz, pkt.startSampleIndex, m_anchorTimeSeconds, targetTimes));
        }

        JYDataPacket aligned;
        aligned.kind = pkt.kind;
        aligned.channelCount = pkt.channelCount;
        aligned.samplesPerChannel = targetTimes.size();
        aligned.sampleRateHz = refRate;
        aligned.startSampleIndex = static_cast<quint64>(qRound64((maxStart - m_anchorTimeSeconds) * refRate));
        aligned.timestampMs = batch.timestampMs;
        interleave(resampled, aligned.data);

        batch.packets.insert(pkt.kind, aligned);
    }

    return true;
}

QVector<QVector<double>> JYDataAligner::deinterleave(const JYDataPacket &packet)
{
    QVector<QVector<double>> channels;
    if (packet.channelCount <= 0) {
        return channels;
    }
    const int samplesPerChannel = packet.samplesPerChannel > 0
        ? packet.samplesPerChannel
        : (packet.data.size() / packet.channelCount);
    channels.resize(packet.channelCount);
    for (int ch = 0; ch < packet.channelCount; ++ch) {
        channels[ch].reserve(samplesPerChannel);
    }
    for (int i = 0; i < packet.data.size(); ++i) {
        const int ch = i % packet.channelCount;
        channels[ch].append(packet.data[i]);
    }
    return channels;
}

QVector<double> JYDataAligner::resampleChannel(const QVector<double> &samples,
                                               double sampleRateHz,
                                               quint64 startIndex,
                                               double t0Seconds,
                                               const QVector<double> &targetTimes)
{
    QVector<double> out;
    out.reserve(targetTimes.size());

    if (samples.isEmpty() || sampleRateHz <= 0.0) {
        out.fill(0.0, targetTimes.size());
        return out;
    }

    const double startTime = t0Seconds + static_cast<double>(startIndex) / sampleRateHz;

    // 对每个目标时刻，先换算成源通道中的浮点样本索引，再做线性插值。
    for (double t : targetTimes) {
        const double targetIndex = (t - startTime) * sampleRateHz;
        const int idx0 = static_cast<int>(qFloor(targetIndex));
        const int idx1 = idx0 + 1;
        if (idx0 < 0 || idx1 >= samples.size()) {
            out.push_back(samples.value(qBound(0, idx0, samples.size() - 1)));
            continue;
        }
        const double t0 = static_cast<double>(idx0);
        const double t1 = static_cast<double>(idx1);
        const double x0 = samples.at(idx0);
        const double x1 = samples.at(idx1);
        const double ratio = (targetIndex - t0) / (t1 - t0);
        out.push_back(x0 + ratio * (x1 - x0));
    }

    return out;
}

void JYDataAligner::interleave(const QVector<QVector<double>> &channels, QVector<double> &out)
{
    out.clear();
    if (channels.isEmpty()) {
        return;
    }
    const int channelCount = channels.size();
    int samplesPerChannel = channels.first().size();
    for (const auto &ch : channels) {
        samplesPerChannel = qMin(samplesPerChannel, ch.size());
    }
    out.reserve(channelCount * samplesPerChannel);
    for (int i = 0; i < samplesPerChannel; ++i) {
        for (int ch = 0; ch < channelCount; ++ch) {
            out.push_back(channels[ch][i]);
        }
    }
}
