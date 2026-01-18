#include "camera.h"
#include <QMessageBox>
#include <QPainter>
#include <QDebug>
#include <QThread>
#include "RtNet.h"
#include <QFileDialog>

Camera::Camera(QWidget *parent)
    : QWidget(parent)
{
    startStream();
}

Camera::~Camera()
{
    stopStream();
}

static void JpegCallback(uint8_t *data, uint32_t dataLen, uint32_t width, uint32_t height, uint64_t pts, void *arg) {
    Camera* camera = static_cast<Camera*>(arg);
    cv::Mat image = cv::imdecode(cv::Mat(1, dataLen, CV_8UC1, data), cv::IMREAD_COLOR);
    std::lock_guard<std::mutex> lock(camera->m_mutex);
    if (!image.empty() && camera->format == CameraModel::RGB) {
        camera->m_imageData.image = image;
        emit camera->Image_data(camera->m_imageData);
        cv::waitKey(1);
    }
}

static void IrJpegCallback(uint8_t *data, uint32_t dataLen, uint32_t width, uint32_t height, uint64_t pts, void *arg) {
    Camera* camera = static_cast<Camera*>(arg);
    cv::Mat image = cv::imdecode(cv::Mat(1, dataLen, CV_8UC1, data), cv::IMREAD_COLOR);
    std::lock_guard<std::mutex> lock(camera->m_mutex);
    if (!image.empty() && camera->format == CameraModel::IR) {
        camera->m_imageData.image = image;
        cv::waitKey(1);
    }
}

static void TempCallback(uint16_t *paru16Data, uint32_t u32Width, uint32_t u32Height, uint64_t u64Pts, void *pArg)
{
    Camera* camera = static_cast<Camera*>(pArg);
    std::lock_guard<std::mutex> lock(camera->m_mutex);
    camera->m_imageData.tempWidth = u32Width;
    camera->m_imageData.tempHeight = u32Height;
    camera->m_imageData.tempData = paru16Data;
    emit camera->Image_data(camera->m_imageData);
}

void Camera::startStream() {
    if (m_state == CameraState::START) {
        return;
    }
    if (RtNet_Init()) {
        QMessageBox::critical(0, "失败", "相机初始化失败");
        return;
    }
    const char* ip = "192.168.0.200";
    ST_FLIP_INFO flipInfo;
    flipInfo.ucFlipType = 2;
    int rs = RtNet_SetImgFlip(ip, &flipInfo);
    if(rs != 0)
    {
        std::cout << "设置图像翻转失败" << std::endl;
    }
    rgbStreamer = RtNet_StartRgbJpegStream(ip, JpegCallback, this);
    irStreamer = RtNet_StartIrJpegStream(ip, IrJpegCallback, this);
    tempStreamer = RtNet_StartTemperatureStream(ip, TempCallback, this);
    m_state = CameraState::START;
}

void Camera::stopStream() {
    {std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == CameraState::STOP) return;}
    RtNet_StopIrJpegStream(irStreamer);
    RtNet_StopRgbJpegStream(rgbStreamer);
    RtNet_StopTemperatureData(tempStreamer);
    RtNet_Exit();
    {std::lock_guard<std::mutex> lock(m_mutex);
    m_state = CameraState::STOP;}
}
