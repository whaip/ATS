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
    struct Config {
        int deviceIndex = 0;
        int width = 1920;
        int height = 1080;
        int fps = 30;
        int fourcc = 0; // 0 means "default"
    };

    static CameraStation *instance();

    // Starts capture thread once; safe to call multiple times.
    void start();
    // Optional: stop thread on app exit.
    void stop();

    // Apply configuration (coalesced to latest).
    void requestConfigure(const Config &config);

    // Snapshot of latest frame.
    bool tryGetLatestImage(ImageData *out) const;

signals:
    void imageCaptured(const ImageData &image);
    void stationStatus(const QString &text);
    void configureFinished(bool ok, int deviceIndex, int width, int height, int fps, int fourcc, const QString &detail);

private:
    explicit CameraStation(QObject *parent = nullptr);
    ~CameraStation() override;

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
