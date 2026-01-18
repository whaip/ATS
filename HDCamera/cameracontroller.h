#ifndef CAMERACONTROLLER_H
#define CAMERACONTROLLER_H

#include "cameratypes.h"
#include <QObject>
#include <QMutex>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>

#include <opencv2/opencv.hpp>

class CameraController : public QObject
{
    Q_OBJECT

public:
    struct CameraConfig {
        int deviceIndex = 0;
        int width = 1920;
        int height = 1080;
        int fps = 30;
        int fourcc = 0; // 0 means "default"
    };

    explicit CameraController(QObject *parent = nullptr);
    ~CameraController() override;

    bool initialize(int deviceIndex = 0);
    bool configure(const CameraConfig &config);
    bool startCamera();
    void stopCamera();

    // Async API (non-blocking on UI thread). Uses an internal queue and a control thread.
    void requestConfigure(const CameraConfig &config, bool startAfterApply);
    void requestStart();
    void requestStop();

    ImageData getLatestImage() const;

signals:
    void statusChanged(CameraStatus status);
    void imageCaptured(const ImageData &image);

    // Emitted when an async configure finishes (or fails) on the control thread.
    void configureFinished(bool ok, int deviceIndex, int width, int height, int fps, int fourcc, const QString &detail);

private:
    // Control thread loop and helpers.
    void controlLoop();
    bool initializeInternal(int deviceIndex);
    bool startCameraInternal();
    void stopCameraInternal();

    void captureLoop();
    void processLoop();
    static ImageData toImageData(const cv::Mat &bgrOrGray);

    bool reopenCaptureWithFps(int newFps);

    std::thread m_controlThread;
    std::mutex m_controlMutex;
    std::condition_variable m_controlCv;
    bool m_shutdown = false;
    bool m_desiredRunning = false;
    bool m_hasPendingConfig = false;
    CameraConfig m_pendingConfig;

    // Serialize initialize/configure/start/stop across threads.
    std::mutex m_opMutex;

    int m_deviceIndex = 0;
    int m_fourcc = 0;

    cv::VideoCapture capture;
    std::atomic<bool> isRunning{false};
    std::unique_ptr<std::thread> captureThread;
    std::unique_ptr<std::thread> processThread;

    std::mutex frameMutex;
    std::condition_variable frameCv;
    std::queue<cv::Mat> frameQueue;
    cv::Mat currentFrame;

    ImageData latestImage;
    mutable QMutex imageMutex;

    int width{1920};
    int height{1080};
    int fps{30};
    int maxQueueSize{5};

    CameraStatus status{CameraStatus::DISCONNECTED};
};

#endif // CAMERACONTROLLER_H
