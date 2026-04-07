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
    // 左侧菜单在主菜单页和相机设置页之间切换。
    void setMenuPage(int index);
    // 初始化相机选择、分辨率、像素格式和帧率相关控件。
    void initCameraSettingsUi();
    void refreshCameraList();
    void refreshResolutionListForCurrentCamera();
    void refreshPixelFormatListForCurrentSelection();
    void refreshFpsListForCurrentSelection();
    // 对频繁变更做延迟合并，避免每次切换下拉框都立即重配相机。
    void scheduleApplyCameraSettings();
    void applyCameraSettingsNow();
    void ensureComponentDetectWorker();
    void applyComponentDetectSettings();
    void refreshComponentDetectPreview();
    // 在原图上叠加元件检测框和 PCB 提取四边形。
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

    // 缓存 DirectShow 能力枚举结果，comboCamera 的 itemData 存的是设备索引。
    QList<QList<DShowCameraUtil::StreamCapability>> m_capabilitiesByDevice;
};

#endif // HDCAMERA_H
