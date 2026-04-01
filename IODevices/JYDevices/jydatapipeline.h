#ifndef JYDATAPIPELINE_H
#define JYDATAPIPELINE_H

#include "jydataaligner.h"

#include <QObject>
#include <QSet>

class JYDataPipeline : public QObject
{
    Q_OBJECT
public:
    explicit JYDataPipeline(QObject *parent = nullptr);

    // 声明对齐输出所需的设备集合；为空时表示“来了什么设备就按当前集合尝试对齐”。
    void setExpectedKinds(const QSet<JYDeviceKind> &kinds);
    // 设置对齐窗口时长和旧包淘汰策略。
    void setAlignSettings(const JYDataAligner::Settings &settings);
    // 设置统一时间锚点，用于把不同设备的 sampleIndex 映射到同一时间轴。
    void setSyncAnchorMs(qint64 anchorMs);

public slots:
    // 接收一个原始数据包：先做基本校验，再原样对外转发，最后交给对齐器处理。
    void ingest(const JYDataPacket &packet);

signals:
    void alignedBatchReady(const JYAlignedBatch &batch);
    void packetRejected(JYDeviceKind kind, const QString &reason);
    void packetIngested(JYDeviceKind kind, int channelCount, int dataSize, qint64 timestampMs);
    void packetReady(const JYDataPacket &packet);

private:
    bool validate(const JYDataPacket &packet, QString *reason) const;

    JYDataAligner *m_aligner = nullptr;
};

#endif // JYDATAPIPELINE_H
