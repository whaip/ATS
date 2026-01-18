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

public slots:
    void ingest(const JYDataPacket &packet);

signals:
    void alignedReady(const JYAlignedBatch &batch);
    void packetDropped(JYDeviceKind kind, const QString &reason);

private:
    void purgeStale(qint64 newestTs);

    Settings m_settings;
    QSet<JYDeviceKind> m_expected;
    QMap<JYDeviceKind, JYDataPacket> m_latest;
};

#endif // JYDATAALIGNER_H
