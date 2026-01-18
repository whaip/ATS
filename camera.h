#ifndef CAMERA_H
#define CAMERA_H

#include <QWidget>
#include <opencv2/opencv.hpp>
#include<opencv2/imgproc/imgproc_c.h>
#include "RtNet.h"
#include <QImage>
#include <qDebug>
#include <QCloseEvent>
#include <QShowEvent>
#include <mutex>

#define g_camera Camera::getInstance()

struct IRImageData {
    cv::Mat image;
    uint16_t *tempData;
    uint32_t tempWidth;
    uint32_t tempHeight;
};

enum class CameraModel {
    RGB,
    IR,
};

enum class CameraState {
    START,
    STOP,
};

class Camera : public QWidget
{
    Q_OBJECT

public:
    static Camera& getInstance(QWidget *parent = nullptr) {
        static Camera instance(parent);
        return instance;
    }
    IRImageData getImageData(){ return m_imageData; };
    void SetFormat(CameraModel format){ this->format = format; };
    void startStream();
    void stopStream();

    CameraState GetCameraState(){ return m_state; };

    IRImageData m_imageData;
    CameraModel format = CameraModel::IR;

    std::mutex m_mutex;
signals:
    void Image_data(const IRImageData imageData);

private:
    explicit Camera(QWidget *parent = nullptr);
    virtual ~Camera();

    JPEG_STREAMER rgbStreamer;
    JPEG_STREAMER irStreamer;
    TEMP_STREAMER tempStreamer;

    CameraState m_state = CameraState::STOP;


    // 禁止拷贝构造和赋值操作
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
};

#endif // CAMERA_H
