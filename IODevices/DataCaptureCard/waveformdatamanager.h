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

// 单条曲线在当前视图窗口内的可绘制数据。
struct WaveformViewSeries {
    QString key;
    QVector<double> x;
    QVector<double> y;
};
Q_DECLARE_METATYPE(WaveformViewSeries)

// 一次视图构建的输出结果，包含多条曲线以及窗口右侧位置。
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

    // 设置波形保留时长，决定内部蓄水池容量。
    void setRetentionSeconds(double seconds);
    // 设置存储点预算，用于限制内存占用。
    void setStoragePointBudget(int points);
    // 指定当前需要维护的曲线集合，未激活曲线会被及时清理。
    void setActiveSeries(const QSet<QString> &seriesKeys);
    static QString seriesKey(JYDeviceKind kind, int channel);

public slots:
    // 追加一批已对齐的采样包，是采集页面的主入口。
    void appendAlignedBatch(const JYAlignedBatch &batch);
    // 开始新一轮采集前重置内部状态。
    void resetForCapture(quint64 epoch);
    void setEpoch(quint64 epoch);
    // 根据当前可视窗口请求异步构建视图数据。
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
    // 视图请求快照，异步构图时使用。
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

    // 每个通道内部采用环形缓冲区保存归一化到目标时间分辨率后的数据。
    struct ChannelSeries {
        QVector<double> reservoir;
        int head = 0;
        int size = 0;
        qint64 firstSeq = 0;
        qint64 nextSeq = 0;
        double lastValue = 0.0;
        bool hasLastValue = false;
    };

    // 将不同设备采样统一映射到 1 MHz 目标时间轴上，便于混合显示。
    static constexpr qint64 kTargetResolutionHz = 1000000;

    static WaveformViewFrame buildFrameFromSeries(const QHash<QString, ChannelSeries> &seriesMap,
                                                  const ViewRequest &request,
                                                  quint64 epoch,
                                                  qint64 reservoirCapacity);
    // 将一段时间内重复保持不变的值扩展写入目标缓冲。
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
