#ifndef CAMERATYPES_H
#define CAMERATYPES_H

#include <QImage>
#include <QMetaType>

enum class CameraStatus {
    DISCONNECTED,
    INITIALIZED,
    RUNNING,
    STOPPED,
    ERROR
};
Q_DECLARE_METATYPE(CameraStatus)

struct ImageData {
    QImage image;
    qint64 timestampMs = 0;
};
Q_DECLARE_METATYPE(ImageData)

#endif // CAMERATYPES_H
