#ifndef JYDATAALIGNER_H
#define JYDATAALIGNER_H

#include "jydevicetype.h"

#include <QObject>
#include <QSet>

class JYDataAligner : public QObject
{
    Q_OBJECT
public:
    struct Settings {
        int windowMs = 10;
        int maxAgeMs = 200;
    };

    explicit JYDataAligner(QObject *parent = nullptr);

    void setSettings(const Settings &settings);
    void setExpectedKinds(const QSet<JYDeviceKind> &kinds);
    void setSyncAnchorMs(qint64 anchorMs);

public slots:
    void ingest(const JYDataPacket &packet);

signals:
    void alignedReady(const JYAlignedBatch &batch);
    void packetDropped(JYDeviceKind kind, const QString &reason);

private:
    void purgeStale(qint64 newestTs);
    bool buildAlignedBatch(JYAlignedBatch &batch);
    static QVector<QVector<double>> deinterleave(const JYDataPacket &packet);
    static QVector<double> resampleChannel(const QVector<double> &samples,
                                           double sampleRateHz,
                                           quint64 startIndex,
                                           double t0Seconds,
                                           const QVector<double> &targetTimes);
    static void interleave(const QVector<QVector<double>> &channels, QVector<double> &out);

    Settings m_settings;
    QSet<JYDeviceKind> m_expected;
    QMap<JYDeviceKind, JYDataPacket> m_latest;
    bool m_hasAnchor = false;
    double m_anchorTimeSeconds = 0.0;
};

#endif // JYDATAALIGNER_H
