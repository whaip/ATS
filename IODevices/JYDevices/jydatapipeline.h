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

    void setExpectedKinds(const QSet<JYDeviceKind> &kinds);
    void setAlignSettings(const JYDataAligner::Settings &settings);
    void setSyncAnchorMs(qint64 anchorMs);

public slots:
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
