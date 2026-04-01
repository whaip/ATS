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
        // 对齐批次的目标窗口长度，单位毫秒。
        int windowMs = 10;
        // 若旧包时间戳小于“最新包时间戳 - maxAgeMs”则被丢弃。
        int maxAgeMs = 200;
    };

    explicit JYDataAligner(QObject *parent = nullptr);

    // 更新当前对齐策略。
    void setSettings(const Settings &settings);
    // 设置期望参与对齐的设备集合；为空表示不限制设备种类。
    void setExpectedKinds(const QSet<JYDeviceKind> &kinds);
    // 设置绝对时间锚点，用于把 sampleIndex 转成统一时间轴上的时间。
    void setSyncAnchorMs(qint64 anchorMs);

public slots:
    // 输入一个原始包，刷新最新包缓存，并尝试生成一个对齐批次。
    void ingest(const JYDataPacket &packet);

signals:
    void alignedReady(const JYAlignedBatch &batch);
    void packetDropped(JYDeviceKind kind, const QString &reason);

private:
    // 按 maxAgeMs 清理相对最新包过旧的数据。
    void purgeStale(qint64 newestTs);
    // 通过“时间区间取交集 + 统一重采样”生成一个对齐批次。
    bool buildAlignedBatch(JYAlignedBatch &batch);
    // 将交织数据拆分成“每通道一组样本”。
    static QVector<QVector<double>> deinterleave(const JYDataPacket &packet);
    // 用线性插值把单通道数据重采样到目标时间轴。
    static QVector<double> resampleChannel(const QVector<double> &samples,
                                           double sampleRateHz,
                                           quint64 startIndex,
                                           double t0Seconds,
                                           const QVector<double> &targetTimes);
    // 将多通道独立数组重新打包成交织布局。
    static void interleave(const QVector<QVector<double>> &channels, QVector<double> &out);

    Settings m_settings;
    QSet<JYDeviceKind> m_expected;
    QMap<JYDeviceKind, JYDataPacket> m_latest;
    bool m_hasAnchor = false;
    double m_anchorTimeSeconds = 0.0;
};

#endif // JYDATAALIGNER_H
