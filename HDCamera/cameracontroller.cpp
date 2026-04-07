#include "cameracontroller.h"
#include "../logger.h"
#include <QDateTime>
#include <QMutexLocker>

#include <chrono>
#include <thread>

#include <opencv2/core/utils/logger.hpp>

namespace {
qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

class ScopedOpenCvLogLevel final
{
public:
    explicit ScopedOpenCvLogLevel(cv::utils::logging::LogLevel level)
        : previous_(cv::utils::logging::setLogLevel(level))
    {
    }

    ~ScopedOpenCvLogLevel()
    {
        cv::utils::logging::setLogLevel(previous_);
    }

    ScopedOpenCvLogLevel(const ScopedOpenCvLogLevel&) = delete;
    ScopedOpenCvLogLevel& operator=(const ScopedOpenCvLogLevel&) = delete;

private:
    cv::utils::logging::LogLevel previous_;
};
}

CameraController::CameraController(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ImageData>("ImageData");
    qRegisterMetaType<CameraStatus>("CameraStatus");

    // 控制线程负责串行执行停止/配置/启动，界面线程只负责投递命令。
    m_controlThread = std::thread(&CameraController::controlLoop, this);
}

CameraController::~CameraController()
{
    requestStop();
    {
        std::lock_guard<std::mutex> lk(m_controlMutex);
        m_shutdown = true;
    }
    m_controlCv.notify_all();
    if (m_controlThread.joinable()) {
        m_controlThread.join();
    }

    // Ensure resources are released.
    stopCameraInternal();
}

void CameraController::requestConfigure(const CameraConfig &config, bool startAfterApply)
{
    {
        std::lock_guard<std::mutex> lk(m_controlMutex);
        m_pendingConfig = config;
        m_hasPendingConfig = true;
        m_desiredRunning = startAfterApply;
    }
    m_controlCv.notify_one();
}

void CameraController::requestStart()
{
    {
        std::lock_guard<std::mutex> lk(m_controlMutex);
        m_desiredRunning = true;
    }
    m_controlCv.notify_one();
}

void CameraController::requestStop()
{
    {
        std::lock_guard<std::mutex> lk(m_controlMutex);
        m_desiredRunning = false;
    }
    m_controlCv.notify_one();
}

void CameraController::controlLoop()
{
    // 控制线程只关心“最终想要的状态”，中间重复请求会被合并。
    while (true) {
        CameraConfig cfg;
        bool doConfig = false;
        bool desiredRun = false;
        bool shutdown = false;

        {
            std::unique_lock<std::mutex> lk(m_controlMutex);
            m_controlCv.wait(lk, [this]() {
                return m_shutdown || m_hasPendingConfig || (m_desiredRunning != isRunning.load());
            });

            shutdown = m_shutdown;
            if (shutdown) {
                break;
            }

            // Coalesce: always apply the latest pending config.
            if (m_hasPendingConfig) {
                cfg = m_pendingConfig;
                doConfig = true;
                m_hasPendingConfig = false;
            }
            desiredRun = m_desiredRunning;
        }

        if (doConfig) {
            const bool wasRunning = isRunning.load();
            if (wasRunning) {
                stopCameraInternal();
            }

            // Apply configuration
            m_deviceIndex = cfg.deviceIndex;
            width = cfg.width;
            height = cfg.height;
            fps = cfg.fps;
            m_fourcc = cfg.fourcc;

            Logger::log(QStringLiteral("HDCamera::configure(async) device=%1 %2x%3@%4 fourcc=%5")
                            .arg(m_deviceIndex)
                            .arg(width)
                            .arg(height)
                            .arg(fps)
                            .arg(m_fourcc),
                        Logger::Level::Info);

            const bool ok = initializeInternal(m_deviceIndex);
            emit configureFinished(ok, m_deviceIndex, width, height, fps, m_fourcc,
                                   ok ? QStringLiteral("OK") : QStringLiteral("FAILED"));

            if (ok && desiredRun) {
                startCameraInternal();
            }
            continue;
        }

        // No config; just reconcile desired running state.
        if (desiredRun && !isRunning.load()) {
            startCameraInternal();
        } else if (!desiredRun && isRunning.load()) {
            stopCameraInternal();
        }
    }
}

bool CameraController::initializeInternal(int deviceIndex)
{
    // Same logic as initialize(), but must not be called while running.
    if (isRunning.load()) {
        Logger::log(QStringLiteral("HDCamera::initializeInternal ignored (running)"), Logger::Level::Warn);
        return false;
    }
    return initialize(deviceIndex);
}

bool CameraController::startCameraInternal()
{
    // Ensure desired state is reflected without blocking UI.
    return startCamera();
}

void CameraController::stopCameraInternal()
{
    stopCamera();
}

bool CameraController::initialize(int deviceIndex)
{
    std::lock_guard<std::mutex> opLock(m_opMutex);
    if (isRunning.load()) {
        Logger::log(QStringLiteral("HDCamera::initialize ignored (running)"), Logger::Level::Warn);
        return false;
    }

    m_deviceIndex = deviceIndex;

    Logger::log(QStringLiteral("HDCamera::initialize device=%1").arg(m_deviceIndex), Logger::Level::Info);

    if (capture.isOpened()) {
        capture.release();
    }

    const int mjpg = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    const bool isHighResRequest = (width >= 4000 || height >= 3000);

    // 高分辨率下若未明确指定像素格式，优先尝试 MJPG 以减轻带宽压力。
    if (isHighResRequest && m_fourcc == 0) {
        m_fourcc = mjpg;
    }

    auto openCapture = [this](cv::VideoCapture &cap) -> bool {
#ifdef _WIN32
        // Our UI enumerates devices via DirectShow; its index ordering matches CAP_DSHOW.
        // So prefer DSHOW to ensure we're opening the intended device.
        {
            ScopedOpenCvLogLevel quiet(cv::utils::logging::LOG_LEVEL_ERROR);
            if (cap.open(m_deviceIndex, cv::CAP_DSHOW)) {
                return true;
            }
        }
        // Fallbacks (index ordering may differ across backends).
        if (cap.open(m_deviceIndex, cv::CAP_MSMF)) {
            return true;
        }
        return cap.open(m_deviceIndex);
#else
        return cap.open(m_deviceIndex);
#endif
    };

    const bool opened = openCapture(capture);
    if (!opened) {
        Logger::log(QStringLiteral("HDCamera open failed device=%1").arg(m_deviceIndex), Logger::Level::Error);
        status = CameraStatus::ERROR;
        emit statusChanged(status);
        return false;
    }

    if (m_fourcc != 0) {
        capture.set(cv::CAP_PROP_FOURCC, m_fourcc);
    }

    capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
    capture.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    capture.set(cv::CAP_PROP_FPS, fps);

    const auto readIntProp = [this](int prop) -> int {
        const double v = capture.get(prop);
        return static_cast<int>(v > 0 ? std::lround(v) : v);
    };
    auto readFourccProp = [this]() -> int {
        const double v = capture.get(cv::CAP_PROP_FOURCC);
        return static_cast<int>(v);
    };

    const QString backendName = QString::fromStdString(capture.getBackendName());
    int actualW = readIntProp(cv::CAP_PROP_FRAME_WIDTH);
    int actualH = readIntProp(cv::CAP_PROP_FRAME_HEIGHT);
    int actualFps = readIntProp(cv::CAP_PROP_FPS);
    int actualFourcc = readFourccProp();

    Logger::log(
        QStringLiteral("HDCamera opened device=%1 backend=%2 request=%3x%4@%5 fourcc=%6 actual=%7x%8@%9 fourcc=%10")
            .arg(m_deviceIndex)
            .arg(backendName)
            .arg(width)
            .arg(height)
            .arg(fps)
            .arg(m_fourcc)
            .arg(actualW)
            .arg(actualH)
            .arg(actualFps)
            .arg(actualFourcc),
        Logger::Level::Info);

    // Validate we can actually read frames. Some modes report as supported but won't stream unless
    // the right backend/format is used (especially for very high resolutions).
    {
        bool readOk = false;
        for (int i = 0; i < 3; ++i) {
            cv::Mat probe;
            if (capture.read(probe) && !probe.empty()) {
                readOk = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        if (!readOk) {
            Logger::log(QStringLiteral("HDCamera initialize: unable to read frames at %1x%2@%3 fourcc=%4")
                            .arg(width)
                            .arg(height)
                            .arg(fps)
                            .arg(m_fourcc),
                        Logger::Level::Error);
            status = CameraStatus::ERROR;
            emit statusChanged(status);
            capture.release();
            return false;
        }
    }

    status = CameraStatus::INITIALIZED;
    emit statusChanged(status);
    return true;
}

bool CameraController::configure(const CameraConfig &config)
{
    std::lock_guard<std::mutex> opLock(m_opMutex);
    const bool wasRunning = isRunning.load();
    if (wasRunning) {
        stopCamera();
    }

    m_deviceIndex = config.deviceIndex;
    width = config.width;
    height = config.height;
    fps = config.fps;
    m_fourcc = config.fourcc;

    Logger::log(QStringLiteral("HDCamera::configure device=%1 %2x%3@%4 fourcc=%5")
                    .arg(m_deviceIndex)
                    .arg(width)
                    .arg(height)
                    .arg(fps)
                    .arg(m_fourcc),
                Logger::Level::Info);

    const bool ok = initialize(m_deviceIndex);
    if (!ok) {
        return false;
    }

    if (wasRunning) {
        return startCamera();
    }
    return true;
}

bool CameraController::startCamera()
{
    std::lock_guard<std::mutex> opLock(m_opMutex);
    if (isRunning.load()) {
        Logger::log(QStringLiteral("HDCamera::startCamera ignored (already running)"), Logger::Level::Debug);
        return true;
    }

    if (!capture.isOpened()) {
        if (!initialize(m_deviceIndex)) {
            return false;
        }
    }

    isRunning.store(true);

    Logger::log(QStringLiteral("HDCamera::startCamera starting threads"), Logger::Level::Info);

    captureThread = std::make_unique<std::thread>(&CameraController::captureLoop, this);
    processThread = std::make_unique<std::thread>(&CameraController::processLoop, this);

    status = CameraStatus::RUNNING;
    emit statusChanged(status);
    return true;
}

void CameraController::stopCamera()
{
    std::lock_guard<std::mutex> opLock(m_opMutex);
    if (!isRunning.exchange(false)) {
        if (capture.isOpened()) {
            capture.release();
        }
        Logger::log(QStringLiteral("HDCamera::stopCamera ignored (not running)"), Logger::Level::Debug);
        return;
    }

    Logger::log(QStringLiteral("HDCamera::stopCamera stopping threads"), Logger::Level::Info);
    frameCv.notify_all();

    if (captureThread && captureThread->joinable()) {
        captureThread->join();
    }
    if (processThread && processThread->joinable()) {
        processThread->join();
    }
    captureThread.reset();
    processThread.reset();

    {
        std::lock_guard<std::mutex> lock(frameMutex);
        std::queue<cv::Mat> empty;
        frameQueue.swap(empty);
        currentFrame.release();
    }

    if (capture.isOpened()) {
        capture.release();
    }

    status = CameraStatus::STOPPED;
    emit statusChanged(status);
}

ImageData CameraController::getLatestImage() const
{
    QMutexLocker locker(&imageMutex);
    return latestImage;
}

void CameraController::captureLoop()
{
    ScopedOpenCvLogLevel quiet(cv::utils::logging::LOG_LEVEL_ERROR);

    const std::vector<int> fpsFallback = {60, 30, 15, 10, 5};
    auto nextLowerFps = [&fpsFallback](int current) -> int {
        for (int f : fpsFallback) {
            if (f < current) {
                return f;
            }
        }
        return current;
    };

    int readFailCount = 0;
    auto lastAdaptiveAttempt = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    // 抓帧线程只负责尽快从驱动取帧并压入队列，不做耗时图像转换。
    while (isRunning.load()) {
        cv::Mat frame;
        if (!capture.read(frame) || frame.empty()) {
            ++readFailCount;
            if (readFailCount == 1 || (readFailCount % 60) == 0) {
                Logger::log(QStringLiteral("HDCamera read failed (%1 times)").arg(readFailCount), Logger::Level::Warn);
            }

            // Adaptive FPS: if we keep failing to grab, try a lower FPS and reopen.
            // This helps high-resolution modes where the camera can't sustain the requested FPS.
            if (readFailCount >= 30) {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastAdaptiveAttempt >= std::chrono::seconds(2)) {
                    lastAdaptiveAttempt = now;

                    const int currentFps = fps;
                    const int newFps = nextLowerFps(currentFps);
                    if (newFps < currentFps && isRunning.load()) {
                        Logger::log(QStringLiteral("Adaptive FPS: %1 -> %2 (reopen capture)").arg(currentFps).arg(newFps),
                                    Logger::Level::Info);
                        const bool reopened = reopenCaptureWithFps(newFps);
                        Logger::log(QStringLiteral("Adaptive reopen -> %1").arg(reopened ? QStringLiteral("OK") : QStringLiteral("FAILED")),
                                    reopened ? Logger::Level::Info : Logger::Level::Warn);
                        if (reopened) {
                            readFailCount = 0;
                        }
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        readFailCount = 0;

        {
            std::lock_guard<std::mutex> lock(frameMutex);
            if (static_cast<int>(frameQueue.size()) >= maxQueueSize) {
                frameQueue.pop();
            }
            frameQueue.push(frame);
        }
        frameCv.notify_one();
    }
}

bool CameraController::reopenCaptureWithFps(int newFps)
{
    if (newFps <= 0) {
        return false;
    }

    // Only called from capture thread (or when camera is not running). Avoid calling stopCamera()/initialize().
    try {
        cv::VideoCapture newCap;

        auto openCapture = [this](cv::VideoCapture &cap) -> bool {
    #ifdef _WIN32
            {
            ScopedOpenCvLogLevel quiet(cv::utils::logging::LOG_LEVEL_ERROR);
            if (cap.open(m_deviceIndex, cv::CAP_DSHOW)) {
                return true;
            }
            }
            if (cap.open(m_deviceIndex, cv::CAP_MSMF)) {
            return true;
            }
            return cap.open(m_deviceIndex);
    #else
            return cap.open(m_deviceIndex);
    #endif
        };

        if (!openCapture(newCap)) {
            return false;
        }

        // Apply current config with updated FPS.
        if (m_fourcc != 0) {
            newCap.set(cv::CAP_PROP_FOURCC, m_fourcc);
        }
        newCap.set(cv::CAP_PROP_FRAME_WIDTH, width);
        newCap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        newCap.set(cv::CAP_PROP_FPS, newFps);

        // Quick sanity check to avoid swapping in a dead capture.
        cv::Mat probe;
        if (!newCap.read(probe) || probe.empty()) {
            return false;
        }

        // Swap in the new capture.
        if (capture.isOpened()) {
            capture.release();
        }
        capture = std::move(newCap);
        fps = newFps;
        return true;
    } catch (...) {
        return false;
    }
}

void CameraController::processLoop()
{
    // 处理线程把 OpenCV Mat 转成 Qt 可用的 QImage，并向外发信号。
    while (isRunning.load()) {
        cv::Mat frame;
        {
            std::unique_lock<std::mutex> lock(frameMutex);
            frameCv.wait(lock, [this]() { return !isRunning.load() || !frameQueue.empty(); });
            if (!isRunning.load()) {
                break;
            }
            frame = frameQueue.front();
            frameQueue.pop();
            currentFrame = frame;
        }

        const ImageData data = toImageData(frame);
        {
            QMutexLocker locker(&imageMutex);
            latestImage = data;
        }
        emit imageCaptured(data);
    }
}

ImageData CameraController::toImageData(const cv::Mat &mat)
{
    ImageData out;
    out.timestampMs = nowMs();

    if (mat.empty()) {
        return out;
    }

    cv::Mat rgb;
    if (mat.channels() == 3) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        out.image = img.copy();
    } else if (mat.channels() == 1) {
        QImage img(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
        out.image = img.copy();
    } else if (mat.channels() == 4) {
        cv::cvtColor(mat, rgb, cv::COLOR_BGRA2RGBA);
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGBA8888);
        out.image = img.copy();
    }

    return out;
}
