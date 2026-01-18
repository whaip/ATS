#include "jydataaligner.h"

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

void JYDataAligner::ingest(const JYDataPacket &packet)
{
    if (m_expected.contains(packet.kind) || m_expected.isEmpty()) {
        m_latest[packet.kind] = packet;
    } else {
        emit packetDropped(packet.kind, QStringLiteral("unexpected device"));
        return;
    }

    qint64 newest = packet.timestampMs;
    purgeStale(newest);

    if (!m_expected.isEmpty()) {
        for (auto kind : m_expected) {
            if (!m_latest.contains(kind)) {
                return;
            }
        }
    }

    qint64 minTs = packet.timestampMs;
    qint64 maxTs = packet.timestampMs;
    for (auto it = m_latest.begin(); it != m_latest.end(); ++it) {
        minTs = std::min(minTs, it->timestampMs);
        maxTs = std::max(maxTs, it->timestampMs);
    }

    if (maxTs - minTs <= m_settings.windowMs) {
        JYAlignedBatch batch;
        batch.packets = m_latest;
        batch.timestampMs = maxTs;
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
