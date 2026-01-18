#ifndef IRCAMERASTATION_H
#define IRCAMERASTATION_H

#include <QByteArray>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "RtNet.h"

class IRCameraStation : public QObject
{
    Q_OBJECT
public:
    struct Frame {
        QImage irImage;
        QVector<double> temperatureMatrix;
        QSize matrixSize;
        qint64 timestampMs = 0;
    };

    struct PointResult {
        QString tag;
        QString id;
        QPointF pos;
        double temp = 0.0;
        qint64 timestampMs = 0;
    };

    struct BoxResult {
        QString tag;
        QString id;
        QRectF rect;
        double minTemp = 0.0;
        double maxTemp = 0.0;
        double avgTemp = 0.0;
        qint64 timestampMs = 0;
    };

    static IRCameraStation *instance();

    void start();
    void stop();
    bool isRunning() const;

    bool tryGetLatestFrame(Frame *out) const;

    void setPointSubscription(const QString &tag, const QString &id, const QPointF &pos);
    void removePointSubscription(const QString &tag, const QString &id);
    void setBoxSubscription(const QString &tag, const QString &id, const QRectF &rect);
    void removeBoxSubscription(const QString &tag, const QString &id);
    void clearSubscriptions(const QString &tag);

signals:
    void frameReady(const IRCameraStation::Frame &frame);
    void pointTemperatureReady(const IRCameraStation::PointResult &result);
    void boxTemperatureReady(const IRCameraStation::BoxResult &result);
    void stationStatus(const QString &text);

private:
    friend void IrJpegCallback(uint8_t *data, uint32_t dataLen, uint32_t width, uint32_t height, uint64_t pts, void *arg);
    friend void TempCallback(uint16_t *data, uint32_t width, uint32_t height, uint64_t pts, void *arg);

    explicit IRCameraStation(QObject *parent = nullptr);
    ~IRCameraStation() override;

    struct QueueEvent {
        enum class Type { IrJpeg, Temp } type;
        QByteArray jpeg;
        QVector<uint16_t> temp;
        uint32_t width = 0;
        uint32_t height = 0;
        qint64 timestampMs = 0;
    };

    struct PointSub {
        QString tag;
        QString id;
        QPointF pos;
    };

    struct BoxSub {
        QString tag;
        QString id;
        QRectF rect;
    };

    void enqueueIrJpeg(const uint8_t *data, uint32_t dataLen, uint32_t width, uint32_t height, qint64 timestampMs);
    void enqueueTemp(const uint16_t *data, uint32_t width, uint32_t height, qint64 timestampMs);
    void workerLoop();
    void emitIfReady(qint64 timestampMs);

    static double sampleTemperature(const QVector<double> &matrix, const QSize &size, const QImage &image, const QPointF &pos);
    static void computeBoxStats(const QVector<double> &matrix, const QSize &size, const QImage &image, const QRectF &rect,
                                double &minT, double &maxT, double &avgT);

    mutable QMutex m_frameMutex;
    Frame m_latestFrame;
    bool m_hasImage = false;
    bool m_hasTemp = false;

    mutable QMutex m_subMutex;
    QVector<PointSub> m_pointSubs;
    QVector<BoxSub> m_boxSubs;

    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<QueueEvent> m_queue;

    std::unique_ptr<std::thread> m_worker;
    std::atomic<bool> m_workerRunning{false};

    mutable std::mutex m_stateMutex;
    bool m_running = false;
    bool m_stopRequested = false;

    JPEG_STREAMER m_irStreamer = nullptr;
    TEMP_STREAMER m_tempStreamer = nullptr;
};

Q_DECLARE_METATYPE(IRCameraStation::Frame)
Q_DECLARE_METATYPE(IRCameraStation::PointResult)
Q_DECLARE_METATYPE(IRCameraStation::BoxResult)

#endif // IRCAMERASTATION_H
