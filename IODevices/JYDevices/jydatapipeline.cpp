#include "jydatapipeline.h"
#include "../../logger.h"

JYDataPipeline::JYDataPipeline(QObject *parent)
    : QObject(parent)
    , m_aligner(new JYDataAligner(this))
{
    connect(m_aligner, &JYDataAligner::alignedReady, this, &JYDataPipeline::alignedBatchReady);
    connect(m_aligner, &JYDataAligner::packetDropped, this, &JYDataPipeline::packetRejected);
}

void JYDataPipeline::setExpectedKinds(const QSet<JYDeviceKind> &kinds)
{
    if (m_aligner) {
        m_aligner->setExpectedKinds(kinds);
    }
}

void JYDataPipeline::setAlignSettings(const JYDataAligner::Settings &settings)
{
    if (m_aligner) {
        m_aligner->setSettings(settings);
    }
}

void JYDataPipeline::setSyncAnchorMs(qint64 anchorMs)
{
    if (m_aligner) {
        m_aligner->setSyncAnchorMs(anchorMs);
    }
}

void JYDataPipeline::ingest(const JYDataPacket &packet)
{
    QString reason;
    if (!validate(packet, &reason)) {
        Logger::log(QStringLiteral("Packet rejected: kind=%1 reason=%2 ch=%3 size=%4")
                        .arg(static_cast<int>(packet.kind))
                        .arg(reason)
                        .arg(packet.channelCount)
                        .arg(packet.data.size()),
                    Logger::Level::Warn);
        emit packetRejected(packet.kind, reason);
        return;
    }
    emit packetIngested(packet.kind, packet.channelCount, packet.data.size(), packet.timestampMs);
    emit packetReady(packet);
    if (m_aligner) {
        m_aligner->ingest(packet);
    }
}

bool JYDataPipeline::validate(const JYDataPacket &packet, QString *reason) const
{
    if (packet.channelCount <= 0) {
        if (reason) *reason = QStringLiteral("invalid channel count");
        return false;
    }
    if (packet.data.isEmpty()) {
        if (reason) *reason = QStringLiteral("empty data");
        return false;
    }
    return true;
}
