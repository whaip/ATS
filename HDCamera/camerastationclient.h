#ifndef CAMERASTATIONCLIENT_H
#define CAMERASTATIONCLIENT_H

#include "camerastation.h"

#include <QObject>

class CameraStationClient : public QObject
{
    Q_OBJECT
public:
    explicit CameraStationClient(QObject *parent = nullptr);

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void requestConfigure(const CameraStation::Config &config);

    bool tryGetLatestImage(ImageData *out) const;

signals:
    void imageCaptured(const ImageData &image);

private slots:
    void onStationImageCaptured(const ImageData &image);

private:
    bool m_enabled = false;
};

#endif // CAMERASTATIONCLIENT_H
