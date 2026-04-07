#ifndef CAMERASTATIONCLIENT_H
#define CAMERASTATIONCLIENT_H

#include "camerastation.h"

#include <QObject>

class CameraStationClient : public QObject
{
    Q_OBJECT
public:
    explicit CameraStationClient(QObject *parent = nullptr);

    // 只控制当前客户端是否转发图像，不影响底层单例采集站是否继续采集。
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    // 将配置请求转发给全局 CameraStation。
    void requestConfigure(const CameraStation::Config &config);

    bool tryGetLatestImage(ImageData *out) const;

signals:
    void imageCaptured(const ImageData &image);

private slots:
    // 单例站点有新图像时，按 enabled 状态决定是否继续向外转发。
    void onStationImageCaptured(const ImageData &image);

private:
    bool m_enabled = false;
};

#endif // CAMERASTATIONCLIENT_H
