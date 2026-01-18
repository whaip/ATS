#include "camerastation.h"

#include "../logger.h"

#include <QDateTime>

#include <chrono>

namespace {
qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}
}

CameraStation *CameraStation::instance()
{
    static CameraStation *s = new CameraStation();
    return s;
}

CameraStation::CameraStation(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ImageData>("ImageData");
    qRegisterMetaType<CameraStation::Config>("CameraStation::Config");
}

CameraStation::~CameraStation()
{
    stop();
}

void CameraStation::start()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_running) {
        return;
    }

    m_running = true;
    m_stopRequested = false;

    // Default active config.
    m_activeConfig = Config();
    m_thread = std::make_unique<std::thread>(&CameraStation::runLoop, this);
}

void CameraStation::stop()
{
    std::unique_ptr<std::thread> th;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_running) {
            return;
        }
        m_stopRequested = true;
        th = std::move(m_thread);
    }

    m_cv.notify_all();

    if (th && th->joinable()) {
        th->join();
    }

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_running = false;
    }
}

void CameraStation::requestConfigure(const Config &config)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_pendingConfig = config;
        m_hasPendingConfig = true;
    }
    m_cv.notify_one();
}

bool CameraStation::tryGetLatestImage(ImageData *out) const
{
    if (!out) {
        return false;
    }

    QMutexLocker locker(&m_imageMutex);
    if (m_latestImage.image.isNull()) {
        return false;
    }
    *out = m_latestImage;
    return true;
}

ImageData CameraStation::toImageData(const cv::Mat &bgrOrGray)
{
    ImageData out;
    out.timestampMs = nowMs();

    if (bgrOrGray.empty()) {
        return out;
    }

    cv::Mat rgb;
    if (bgrOrGray.type() == CV_8UC3) {
        cv::cvtColor(bgrOrGray, rgb, cv::COLOR_BGR2RGB);
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        out.image = img.copy();
        return out;
    }

    if (bgrOrGray.type() == CV_8UC1) {
        QImage img(bgrOrGray.data, bgrOrGray.cols, bgrOrGray.rows, static_cast<int>(bgrOrGray.step), QImage::Format_Grayscale8);
        out.image = img.copy();
        return out;
    }

    cv::Mat bgr8;
    bgrOrGray.convertTo(bgr8, CV_8U);
    return toImageData(bgr8);
}

bool CameraStation::openCaptureLocked(const Config &cfg, QString *detailOut)
{
    if (m_capture.isOpened()) {
        m_capture.release();
    }

    auto setDetail = [detailOut](const QString &s) {
        if (detailOut) {
            *detailOut = s;
        }
    };

#ifdef _WIN32
    // Prefer DSHOW to match UI enumeration order.
    if (!m_capture.open(cfg.deviceIndex, cv::CAP_DSHOW)) {
        if (!m_capture.open(cfg.deviceIndex, cv::CAP_MSMF)) {
            if (!m_capture.open(cfg.deviceIndex)) {
                setDetail(QStringLiteral("open failed"));
                return false;
            }
        }
    }
#else
    if (!m_capture.open(cfg.deviceIndex)) {
        setDetail(QStringLiteral("open failed"));
        return false;
    }
#endif

    if (cfg.fourcc != 0) {
        m_capture.set(cv::CAP_PROP_FOURCC, cfg.fourcc);
    }

    m_capture.set(cv::CAP_PROP_FRAME_WIDTH, cfg.width);
    m_capture.set(cv::CAP_PROP_FRAME_HEIGHT, cfg.height);
    m_capture.set(cv::CAP_PROP_FPS, cfg.fps);

    // Probe frames.
    bool ok = false;
    for (int i = 0; i < 3; ++i) {
        cv::Mat probe;
        if (m_capture.read(probe) && !probe.empty()) {
            ok = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    if (!ok) {
        m_capture.release();
        setDetail(QStringLiteral("read probe failed"));
        return false;
    }

    setDetail(QStringLiteral("OK"));
    return true;
}

void CameraStation::closeCaptureLocked()
{
    if (m_capture.isOpened()) {
        m_capture.release();
    }
}

void CameraStation::runLoop()
{
    emit stationStatus(QStringLiteral("Camera station started"));

    // Initial open.
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        QString detail;
        const bool ok = openCaptureLocked(m_activeConfig, &detail);
        m_captureOk.store(ok);
        emit configureFinished(ok, m_activeConfig.deviceIndex, m_activeConfig.width, m_activeConfig.height, m_activeConfig.fps, m_activeConfig.fourcc, detail);

        Logger::log(QStringLiteral("CameraStation initial open dev=%1 %2x%3@%4 fourcc=%5 -> %6 (%7)")
                        .arg(m_activeConfig.deviceIndex)
                        .arg(m_activeConfig.width)
                        .arg(m_activeConfig.height)
                        .arg(m_activeConfig.fps)
                        .arg(m_activeConfig.fourcc)
                        .arg(ok ? QStringLiteral("OK") : QStringLiteral("FAILED"))
                        .arg(detail),
                    ok ? Logger::Level::Info : Logger::Level::Warn);
    }

    cv::Mat frame;

    while (true) {
        // Apply pending config (coalesced).
        Config cfg;
        bool doConfig = false;
        bool stop = false;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            if (!m_hasPendingConfig && !m_stopRequested) {
                // Don't block too long; keep capturing.
                m_cv.wait_for(lk, std::chrono::milliseconds(1), [this]() { return m_stopRequested || m_hasPendingConfig; });
            }

            stop = m_stopRequested;
            if (stop) {
                closeCaptureLocked();
                break;
            }

            if (m_hasPendingConfig) {
                cfg = m_pendingConfig;
                m_hasPendingConfig = false;
                doConfig = true;
            }
        }

        if (doConfig) {
            QString detail;
            bool ok = false;
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                ok = openCaptureLocked(cfg, &detail);
                if (ok) {
                    m_activeConfig = cfg;
                }
                m_captureOk.store(ok);
            }

            emit configureFinished(ok, cfg.deviceIndex, cfg.width, cfg.height, cfg.fps, cfg.fourcc, detail);

            Logger::log(QStringLiteral("CameraStation configure dev=%1 %2x%3@%4 fourcc=%5 -> %6 (%7)")
                            .arg(cfg.deviceIndex)
                            .arg(cfg.width)
                            .arg(cfg.height)
                            .arg(cfg.fps)
                            .arg(cfg.fourcc)
                            .arg(ok ? QStringLiteral("OK") : QStringLiteral("FAILED"))
                            .arg(detail),
                        ok ? Logger::Level::Info : Logger::Level::Warn);
        }

        bool readOk = false;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_capture.isOpened()) {
                readOk = m_capture.read(frame) && !frame.empty();
            }
        }

        if (!readOk) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        ImageData img = toImageData(frame);
        {
            QMutexLocker locker(&m_imageMutex);
            m_latestImage = img;
        }
        emit imageCaptured(img);

        // Light pacing; camera read already blocks, but avoid tight loop on some backends.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    emit stationStatus(QStringLiteral("Camera station stopped"));
}
