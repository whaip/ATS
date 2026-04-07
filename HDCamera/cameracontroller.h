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
    // 相机配置快照，既可同步调用，也可投递到异步控制线程。
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

    // 异步接口：由内部控制线程串行执行配置/启动/停止，避免 UI 阻塞。
    void requestConfigure(const CameraConfig &config, bool startAfterApply);
    void requestStart();
    void requestStop();

    ImageData getLatestImage() const;

signals:
    void statusChanged(CameraStatus status);
    void imageCaptured(const ImageData &image);

    // 异步配置结束后发出，无论成功还是失败都会带回最终结果。
    void configureFinished(bool ok, int deviceIndex, int width, int height, int fps, int fourcc, const QString &detail);

private:
    // 控制线程主循环及其辅助函数。
    void controlLoop();
    bool initializeInternal(int deviceIndex);
    bool startCameraInternal();
    void stopCameraInternal();

    // 抓帧线程负责从 VideoCapture 拉帧，处理线程负责转成 QImage 并分发给 Qt 层。
    void captureLoop();
    void processLoop();
    static ImageData toImageData(const cv::Mat &bgrOrGray);

    // 采集失败时尝试降低 FPS 重新打开设备。
    bool reopenCaptureWithFps(int newFps);

    std::thread m_controlThread;
    std::mutex m_controlMutex;
    std::condition_variable m_controlCv;
    bool m_shutdown = false;
    bool m_desiredRunning = false;
    bool m_hasPendingConfig = false;
    CameraConfig m_pendingConfig;

    // 用于串行化 initialize/configure/start/stop，避免多线程并发操作同一相机句柄。
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
