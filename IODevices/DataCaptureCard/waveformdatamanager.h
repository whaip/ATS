#ifndef WAVEFORMDATAMANAGER_H
#define WAVEFORMDATAMANAGER_H

#include <QObject>
#include <QFutureWatcher>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "../JYDevices/jydevicetype.h"

struct WaveformViewSeries {
    QString key;
    QVector<double> x;
    QVector<double> y;
};
Q_DECLARE_METATYPE(WaveformViewSeries)

struct WaveformViewFrame {
    QVector<WaveformViewSeries> series;
    double latestX = 0.0;
    quint64 requestId = 0;
    quint64 epoch = 0;
};
Q_DECLARE_METATYPE(WaveformViewFrame)

class WaveformDataManager : public QObject
{
    Q_OBJECT

public:
    explicit WaveformDataManager(QObject *parent = nullptr);

    void setRetentionSeconds(double seconds);
    void setStoragePointBudget(int points);
    void setActiveSeries(const QSet<QString> &seriesKeys);
    static QString seriesKey(JYDeviceKind kind, int channel);

public slots:
    void appendAlignedBatch(const JYAlignedBatch &batch);
    void resetForCapture(quint64 epoch);
    void setEpoch(quint64 epoch);
    void requestView(const QStringList &seriesKeys,
                     double xLower,
                     double xUpper,
                     double baseWindowSeconds,
                     int pixelWidth,
                     bool autoFollow);
    void clear();

signals:
    void viewReady(const WaveformViewFrame &frame);

private:
    struct ViewRequest {
        QStringList seriesKeys;
        double xLower = 0.0;
        double xUpper = 0.0;
        double baseWindowSeconds = 1.0;
        int pixelWidth = 0;
        bool autoFollow = true;
        quint64 requestId = 0;
        bool valid = false;
    };

    struct ChannelSeries {
        QVector<double> reservoir;
        int head = 0;
        int size = 0;
        qint64 firstSeq = 0;
        qint64 nextSeq = 0;
        double lastValue = 0.0;
        bool hasLastValue = false;
    };

    static constexpr qint64 kTargetResolutionHz = 1000000;

    static WaveformViewFrame buildFrameFromSeries(const QHash<QString, ChannelSeries> &seriesMap,
                                                  const ViewRequest &request,
                                                  quint64 epoch,
                                                  qint64 reservoirCapacity);
    static void appendRepeated(ChannelSeries *series,
                               qint64 capacity,
                               qint64 beginSeq,
                               qint64 endSeq,
                               double value);
    static double valueAt(const ChannelSeries &series, qint64 capacity, qint64 seq);
    static WaveformViewSeries buildDenseSeries(const QString &key,
                                               const ChannelSeries &series,
                                               qint64 capacity,
                                               qint64 beginSeq,
                                               qint64 endSeq,
                                               qint64 windowStartSeq);
    static WaveformViewSeries buildBucketedSeries(const QString &key,
                                                  const ChannelSeries &series,
                                                  qint64 capacity,
                                                  qint64 beginSeq,
                                                  qint64 endSeq,
                                                  int pixelWidth,
                                                  qint64 windowStartSeq);

    void appendPacket(JYDeviceKind kind, const JYDataPacket &packet);
    void processPendingView();
    qint64 reservoirCapacity() const;

    mutable QMutex m_mutex;
    QHash<QString, ChannelSeries> m_series;
    QSet<QString> m_activeSeries;
    double m_retentionSeconds = 3.0;
    int m_storagePointBudget = 0;
    ViewRequest m_pendingRequest;
    bool m_viewBuildRunning = false;
    quint64 m_nextRequestId = 1;
    quint64 m_epoch = 0;
    QFutureWatcher<WaveformViewFrame> *m_viewWatcher = nullptr;
};

#endif // WAVEFORMDATAMANAGER_H
