#ifndef HDCAMERA_H
#define HDCAMERA_H

#include "dshowcamerautil.h"
#include "../ComponentsDetect/componenttypes.h"
#include <QList>
#include <QImage>
#include <QPolygonF>
#include <QWidget>

class QTimer;

namespace Ui {
class HDCamera;
}

class CameraStationClient;
class YoloStationClient;

class HDCamera : public QWidget
{
    Q_OBJECT

public:
    explicit HDCamera(QWidget *parent = nullptr);
    ~HDCamera();

    void setMenuVisible(bool visible);
    bool isMenuVisible() const;

private slots:
    void toggleMenuVisible();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void setMenuPage(int index);
    void initCameraSettingsUi();
    void refreshCameraList();
    void refreshResolutionListForCurrentCamera();
    void refreshPixelFormatListForCurrentSelection();
    void refreshFpsListForCurrentSelection();
    void scheduleApplyCameraSettings();
    void applyCameraSettingsNow();
    void ensureComponentDetectWorker();
    void applyComponentDetectSettings();
    void refreshComponentDetectPreview();
    QImage renderComponentDetectOverlay(const QImage &frame,
                                       const QList<CompLabel> &labels,
                                       const QPolygonF &pcbQuad) const;

    Ui::HDCamera *ui;
    bool m_menuVisible = true;
    CameraStationClient *m_camera = nullptr;
    QTimer *m_applySettingsTimer = nullptr;

    YoloStationClient *m_compWorker = nullptr;
    bool m_componentDetectEnabled = false;
    bool m_usePcbExtract = false;
    bool m_drawExtractQuad = false;

    QImage m_lastPreviewFrame;
    QImage m_lastDetectFrame;
    QList<CompLabel> m_lastDetectLabels;
    QPolygonF m_lastDetectQuad;

    // Cached from DirectShow enumeration. comboCamera item data stores the index into this list.
    QList<QList<DShowCameraUtil::StreamCapability>> m_capabilitiesByDevice;
};

#endif // HDCAMERA_H
