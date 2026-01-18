#ifndef COMPONENTSDETECT_H
#define COMPONENTSDETECT_H

#include <QWidget>
#include "componenttypes.h"
#include <QList>
#include <QSize>
#include <QImage>
#include <QPolygonF>

class QStandardItemModel;

class QEvent;

class CameraStationClient;
class YoloStationClient;

namespace DShowCameraUtil {
struct StreamCapability;
}

namespace Ui {
class ComponentsDetect;
}

class ComponentsDetect : public QWidget
{
    Q_OBJECT

public:
    explicit ComponentsDetect(QWidget *parent = nullptr);
    ~ComponentsDetect();

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onYoloResultReady(const QImage &frame,
                          const QList<CompLabel> &labels,
                          const QPolygonF &pcbQuad,
                          qint64 timestampMs,
                          double inferMs);

    void onTableSelectionChanged();

private:
    void applyLocalStyleForTheme();
    void startPipeline();
    void stopPipeline();
    void updateSelectionDetails(const CompLabel *label);

    void refreshOverlayPreview();
    QImage renderOverlay(const QImage &frame, const QList<CompLabel> &labels, const QPolygonF &pcbQuad) const;

    void initClassSelectUi();
    void updateComboSelectText();
    void setAllClassesChecked(bool checked);
    void syncAllItemState();
    QList<int> checkedClassIds() const;
    void applyClassDisplayToWorker();

    void initCameraSettingsUi();
    void refreshCameraList();
    void refreshResolutionListForCurrentCamera();
    void refreshPixelFormatListForCurrentSelection();
    void refreshFpsListForCurrentSelection();
    void applyCameraSettingsNow();

    Ui::ComponentsDetect *ui;

    CameraStationClient *m_camera = nullptr;
    YoloStationClient *m_worker = nullptr;

    QList<CompLabel> m_lastLabels;
    QImage m_lastFrame;
    QPolygonF m_lastPcbQuad;

    QList<QList<DShowCameraUtil::StreamCapability>> m_capsByDevice;
    QTimer *m_applySettingsTimer = nullptr;

    QStandardItemModel *m_classSelectModel = nullptr;
    QStringList m_classNames;
    bool m_updatingClassSelect = false;
};

#endif // COMPONENTSDETECT_H
