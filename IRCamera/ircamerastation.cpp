#include "ircamerastation.h"

#include "../logger.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QThread>

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace {
qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

constexpr const char *kDefaultIp = "192.168.0.200";
}

static void IrJpegCallback(uint8_t *data, uint32_t dataLen, uint32_t width, uint32_t height, uint64_t pts, void *arg)
{
    auto *station = static_cast<IRCameraStation *>(arg);
    if (!station || !data || dataLen == 0) {
        return;
    }
    station->enqueueIrJpeg(data, dataLen, width, height, static_cast<qint64>(pts));
}

static void TempCallback(uint16_t *data, uint32_t width, uint32_t height, uint64_t pts, void *arg)
{
    auto *station = static_cast<IRCameraStation *>(arg);
    if (!station || !data || width == 0 || height == 0) {
        return;
    }
    station->enqueueTemp(data, width, height, static_cast<qint64>(pts));
}

IRCameraStation *IRCameraStation::instance()
{
    static IRCameraStation *s = new IRCameraStation();
    return s;
}

IRCameraStation::IRCameraStation(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<IRCameraStation::Frame>("IRCameraStation::Frame");
    qRegisterMetaType<IRCameraStation::PointResult>("IRCameraStation::PointResult");
    qRegisterMetaType<IRCameraStation::BoxResult>("IRCameraStation::BoxResult");
}

IRCameraStation::~IRCameraStation()
{
    stop();
}

void IRCameraStation::start()
{
    std::lock_guard<std::mutex> lk(m_stateMutex);
    if (m_running) {
        return;
    }

    m_stopRequested = false;
    {
        std::lock_guard<std::mutex> qlk(m_queueMutex);
        m_queue.clear();
    }
    {
        QMutexLocker locker(&m_frameMutex);
        m_latestFrame = Frame();
        m_hasImage = false;
        m_hasTemp = false;
    }

    if (RtNet_Init()) {
        emit stationStatus(QStringLiteral("IR camera init failed"));
        Logger::log(QStringLiteral("IRCameraStation init failed"), Logger::Level::Error);
        return;
    }

    ST_FLIP_INFO flipInfo;
    flipInfo.ucFlipType = 2;
    const int flipResult = RtNet_SetImgFlip(kDefaultIp, &flipInfo);
    if (flipResult != 0) {
        Logger::log(QStringLiteral("IRCameraStation set flip failed"), Logger::Level::Warn);
    }

    m_irStreamer = RtNet_StartIrJpegStream(kDefaultIp, IrJpegCallback, this);
    m_tempStreamer = RtNet_StartTemperatureStream(kDefaultIp, TempCallback, this);

    m_workerRunning.store(true);
    m_worker = std::make_unique<std::thread>(&IRCameraStation::workerLoop, this);

    m_running = true;
    emit stationStatus(QStringLiteral("IR camera station started"));
}

void IRCameraStation::stop()
{
    std::unique_ptr<std::thread> worker;
    {
        std::lock_guard<std::mutex> lk(m_stateMutex);
        if (!m_running) {
            return;
        }
        m_stopRequested = true;
        worker = std::move(m_worker);
    }

    m_queueCv.notify_all();
    if (worker && worker->joinable()) {
        worker->join();
    }

    // Vendor shutdown can be unstable; keep the same order as provided samples.
    RtNet_StopIrJpegStream(m_irStreamer);
    RtNet_StopTemperatureData(m_tempStreamer);
    RtNet_Exit();

    m_irStreamer = nullptr;
    m_tempStreamer = nullptr;

    {
        std::lock_guard<std::mutex> lk(m_stateMutex);
        m_running = false;
    }

    emit stationStatus(QStringLiteral("IR camera station stopped"));
}

bool IRCameraStation::isRunning() const
{
    std::lock_guard<std::mutex> lk(m_stateMutex);
    return m_running;
}

bool IRCameraStation::tryGetLatestFrame(Frame *out) const
{
    if (!out) {
        return false;
    }

    QMutexLocker locker(&m_frameMutex);
    if (!m_hasImage || !m_hasTemp || m_latestFrame.irImage.isNull() || m_latestFrame.temperatureMatrix.isEmpty()) {
        return false;
    }
    *out = m_latestFrame;
    return true;
}

void IRCameraStation::setPointSubscription(const QString &tag, const QString &id, const QPointF &pos)
{
    QMutexLocker locker(&m_subMutex);
    for (auto &sub : m_pointSubs) {
        if (sub.tag == tag && sub.id == id) {
            sub.pos = pos;
            return;
        }
    }
    m_pointSubs.push_back(PointSub{tag, id, pos});
}

void IRCameraStation::removePointSubscription(const QString &tag, const QString &id)
{
    QMutexLocker locker(&m_subMutex);
    for (int i = m_pointSubs.size() - 1; i >= 0; --i) {
        if (m_pointSubs[i].tag == tag && m_pointSubs[i].id == id) {
            m_pointSubs.removeAt(i);
        }
    }
}

void IRCameraStation::setBoxSubscription(const QString &tag, const QString &id, const QRectF &rect)
{
    QMutexLocker locker(&m_subMutex);
    for (auto &sub : m_boxSubs) {
        if (sub.tag == tag && sub.id == id) {
            sub.rect = rect;
            return;
        }
    }
    m_boxSubs.push_back(BoxSub{tag, id, rect});
}

void IRCameraStation::removeBoxSubscription(const QString &tag, const QString &id)
{
    QMutexLocker locker(&m_subMutex);
    for (int i = m_boxSubs.size() - 1; i >= 0; --i) {
        if (m_boxSubs[i].tag == tag && m_boxSubs[i].id == id) {
            m_boxSubs.removeAt(i);
        }
    }
}

void IRCameraStation::clearSubscriptions(const QString &tag)
{
    QMutexLocker locker(&m_subMutex);
    for (int i = m_pointSubs.size() - 1; i >= 0; --i) {
        if (m_pointSubs[i].tag == tag) {
            m_pointSubs.removeAt(i);
        }
    }
    for (int i = m_boxSubs.size() - 1; i >= 0; --i) {
        if (m_boxSubs[i].tag == tag) {
            m_boxSubs.removeAt(i);
        }
    }
}

void IRCameraStation::enqueueIrJpeg(const uint8_t *data, uint32_t dataLen, uint32_t width, uint32_t height, qint64 timestampMs)
{
    QueueEvent ev;
    ev.type = QueueEvent::Type::IrJpeg;
    ev.jpeg = QByteArray(reinterpret_cast<const char *>(data), static_cast<int>(dataLen));
    ev.width = width;
    ev.height = height;
    ev.timestampMs = timestampMs > 0 ? timestampMs : nowMs();

    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        if (m_queue.size() > 2) {
            m_queue.pop_front();
        }
        m_queue.push_back(std::move(ev));
    }
    m_queueCv.notify_one();
}

void IRCameraStation::enqueueTemp(const uint16_t *data, uint32_t width, uint32_t height, qint64 timestampMs)
{
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (count == 0) {
        return;
    }

    QueueEvent ev;
    ev.type = QueueEvent::Type::Temp;
    ev.temp.resize(static_cast<int>(count));
    std::memcpy(ev.temp.data(), data, count * sizeof(uint16_t));
    ev.width = width;
    ev.height = height;
    ev.timestampMs = timestampMs > 0 ? timestampMs : nowMs();

    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        if (m_queue.size() > 2) {
            m_queue.pop_front();
        }
        m_queue.push_back(std::move(ev));
    }
    m_queueCv.notify_one();
}

void IRCameraStation::workerLoop()
{
    while (m_workerRunning.load()) {
        QueueEvent ev;
        {
            std::unique_lock<std::mutex> lk(m_queueMutex);
            m_queueCv.wait_for(lk, std::chrono::milliseconds(5), [this]() {
                return !m_queue.empty() || m_stopRequested;
            });
            if (m_stopRequested) {
                break;
            }
            if (m_queue.empty()) {
                continue;
            }
            ev = std::move(m_queue.front());
            m_queue.pop_front();
        }

        if (ev.type == QueueEvent::Type::IrJpeg) {
            cv::Mat raw(1, ev.jpeg.size(), CV_8UC1, const_cast<char *>(ev.jpeg.data()));
            cv::Mat image = cv::imdecode(raw, cv::IMREAD_COLOR);
            if (!image.empty()) {
                cv::Mat rgb;
                cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
                QImage qimg(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
                QImage imgCopy = qimg.copy();

                {
                    QMutexLocker locker(&m_frameMutex);
                    m_latestFrame.irImage = imgCopy;
                    m_latestFrame.timestampMs = ev.timestampMs;
                    m_hasImage = true;
                }
                emitIfReady(ev.timestampMs);
            }
        } else if (ev.type == QueueEvent::Type::Temp) {
            QVector<double> temps;
            temps.reserve(ev.temp.size());
            for (uint16_t v : ev.temp) {
                temps.push_back(static_cast<double>(v) * 0.1);
            }

            {
                QMutexLocker locker(&m_frameMutex);
                m_latestFrame.temperatureMatrix = temps;
                m_latestFrame.matrixSize = QSize(static_cast<int>(ev.width), static_cast<int>(ev.height));
                m_latestFrame.timestampMs = ev.timestampMs;
                m_hasTemp = true;
            }
            emitIfReady(ev.timestampMs);
        }
    }

    m_workerRunning.store(false);
}

void IRCameraStation::emitIfReady(qint64 timestampMs)
{
    Frame frame;
    {
        QMutexLocker locker(&m_frameMutex);
        if (!m_hasImage || !m_hasTemp || m_latestFrame.irImage.isNull() || m_latestFrame.temperatureMatrix.isEmpty()) {
            return;
        }
        frame = m_latestFrame;
        frame.timestampMs = timestampMs > 0 ? timestampMs : frame.timestampMs;
    }

    emit frameReady(frame);

    QVector<PointSub> points;
    QVector<BoxSub> boxes;
    {
        QMutexLocker locker(&m_subMutex);
        points = m_pointSubs;
        boxes = m_boxSubs;
    }

    for (const auto &p : points) {
        PointResult res;
        res.tag = p.tag;
        res.id = p.id;
        res.pos = p.pos;
        res.temp = sampleTemperature(frame.temperatureMatrix, frame.matrixSize, frame.irImage, p.pos);
        res.timestampMs = frame.timestampMs;
        emit pointTemperatureReady(res);
    }

    for (const auto &b : boxes) {
        BoxResult res;
        res.tag = b.tag;
        res.id = b.id;
        res.rect = b.rect;
        computeBoxStats(frame.temperatureMatrix, frame.matrixSize, frame.irImage, b.rect, res.minTemp, res.maxTemp, res.avgTemp);
        res.timestampMs = frame.timestampMs;
        emit boxTemperatureReady(res);
    }
}

double IRCameraStation::sampleTemperature(const QVector<double> &matrix, const QSize &size, const QImage &image, const QPointF &pos)
{
    if (matrix.isEmpty() || !size.isValid() || image.isNull()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double imgW = image.width();
    const double imgH = image.height();
    if (imgW <= 0.0 || imgH <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    int tx = static_cast<int>(pos.x() / imgW * size.width());
    int ty = static_cast<int>(pos.y() / imgH * size.height());
    tx = std::clamp(tx, 0, size.width() - 1);
    ty = std::clamp(ty, 0, size.height() - 1);

    const int idx = ty * size.width() + tx;
    if (idx < 0 || idx >= matrix.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return matrix[idx];
}

void IRCameraStation::computeBoxStats(const QVector<double> &matrix, const QSize &size, const QImage &image, const QRectF &rect,
                                      double &minT, double &maxT, double &avgT)
{
    minT = std::numeric_limits<double>::infinity();
    maxT = -std::numeric_limits<double>::infinity();
    avgT = std::numeric_limits<double>::quiet_NaN();

    if (matrix.isEmpty() || !size.isValid() || image.isNull()) {
        return;
    }

    QRectF bounds(0, 0, image.width(), image.height());
    QRectF r = rect.normalized().intersected(bounds);
    if (r.width() <= 0.0 || r.height() <= 0.0) {
        return;
    }

    const int x0 = static_cast<int>(r.left() / image.width() * size.width());
    const int y0 = static_cast<int>(r.top() / image.height() * size.height());
    const int x1 = static_cast<int>(r.right() / image.width() * size.width());
    const int y1 = static_cast<int>(r.bottom() / image.height() * size.height());

    const int xmin = std::clamp(std::min(x0, x1), 0, size.width() - 1);
    const int xmax = std::clamp(std::max(x0, x1), 0, size.width() - 1);
    const int ymin = std::clamp(std::min(y0, y1), 0, size.height() - 1);
    const int ymax = std::clamp(std::max(y0, y1), 0, size.height() - 1);

    double sum = 0.0;
    int count = 0;
    for (int y = ymin; y <= ymax; ++y) {
        const int row = y * size.width();
        for (int x = xmin; x <= xmax; ++x) {
            const double v = matrix[row + x];
            minT = std::min(minT, v);
            maxT = std::max(maxT, v);
            sum += v;
            ++count;
        }
    }

    if (count > 0) {
        avgT = sum / count;
    }
}
