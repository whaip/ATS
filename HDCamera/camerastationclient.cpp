#include "camerastationclient.h"

CameraStationClient::CameraStationClient(QObject *parent)
    : QObject(parent)
{
    // 客户端只做轻量转发，不直接持有底层相机设备。
    connect(CameraStation::instance(), &CameraStation::imageCaptured,
            this, &CameraStationClient::onStationImageCaptured,
            Qt::QueuedConnection);
}

void CameraStationClient::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void CameraStationClient::requestConfigure(const CameraStation::Config &config)
{
    CameraStation::instance()->requestConfigure(config);
}

bool CameraStationClient::tryGetLatestImage(ImageData *out) const
{
    return CameraStation::instance()->tryGetLatestImage(out);
}

void CameraStationClient::onStationImageCaptured(const ImageData &image)
{
    if (!m_enabled) {
        return;
    }
    emit imageCaptured(image);
}
