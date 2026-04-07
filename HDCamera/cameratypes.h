#ifndef CAMERATYPES_H
#define CAMERATYPES_H

#include <QImage>
#include <QMetaType>

// 相机模块对外暴露的运行状态。
enum class CameraStatus {
    DISCONNECTED,
    INITIALIZED,
    RUNNING,
    STOPPED,
    ERROR
};
Q_DECLARE_METATYPE(CameraStatus)

// 一帧图像及其时间戳，作为线程间传递的基础数据结构。
struct ImageData {
    QImage image;
    qint64 timestampMs = 0;
};
Q_DECLARE_METATYPE(ImageData)

#endif // CAMERATYPES_H
