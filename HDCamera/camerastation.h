#ifndef CAMERASTATION_H
#define CAMERASTATION_H

#include "cameratypes.h"

#include <QObject>
#include <QMutex>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <opencv2/opencv.hpp>

class CameraStation : public QObject
{
    Q_OBJECT
public:
    // 单例采集站使用的配置对象，表示当前应保持的打开参数。
    struct Config {
        int deviceIndex = 0;
        int width = 1920;
        int height = 1080;
        int fps = 30;
        int fourcc = 0; // 0 means "default"
    };

    static CameraStation *instance();

    // 启动后台采集线程，重复调用安全。
    void start();
    // 应用退出前可显式停止线程并释放设备。
    void stop();

    // 投递配置变更，请求会合并成“最后一次配置”。
    void requestConfigure(const Config &config);

    // 获取最近一帧图像快照。
    bool tryGetLatestImage(ImageData *out) const;

signals:
    void imageCaptured(const ImageData &image);
    void stationStatus(const QString &text);
    void configureFinished(bool ok, int deviceIndex, int width, int height, int fps, int fourcc, const QString &detail);

private:
    explicit CameraStation(QObject *parent = nullptr);
    ~CameraStation() override;

    // 单例后台线程主循环，负责持续采集和按需重配。
    void runLoop();
    bool openCaptureLocked(const Config &cfg, QString *detailOut);
    void closeCaptureLocked();

    static ImageData toImageData(const cv::Mat &bgrOrGray);

    mutable QMutex m_imageMutex;
    ImageData m_latestImage;

    std::mutex m_mutex;
    std::condition_variable m_cv;

    bool m_running = false;
    bool m_stopRequested = false;

    bool m_hasPendingConfig = false;
    Config m_pendingConfig;
    Config m_activeConfig;

    cv::VideoCapture m_capture;
    std::unique_ptr<std::thread> m_thread;

    std::atomic<bool> m_captureOk{false};
};

Q_DECLARE_METATYPE(CameraStation::Config)

#endif // CAMERASTATION_H
