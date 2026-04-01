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
    // packetReady 保留原始设备粒度的数据；
    // alignedBatchReady 则由 aligner 在后续按统一时间轴重建。
    emit packetIngested(packet.kind, packet.channelCount, packet.data.size(), packet.timestampMs);
    emit packetReady(packet);
    if (m_aligner) {
        m_aligner->ingest(packet);
    }
}

bool JYDataPipeline::validate(const JYDataPacket &packet, QString *reason) const
{
    // 这里只做最基础的结构校验，采样率/对齐窗口等约束交由 aligner 处理。
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
