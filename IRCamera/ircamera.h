#ifndef IRCAMERA_H
#define IRCAMERA_H

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QVector>
#include <limits>
#include <QWidget>

#include "ircamerastation.h"

class QGraphicsEllipseItem;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsSimpleTextItem;
class QTableWidgetItem;
class ZoomableGraphicsView;
class IRCameraStationClient;

namespace Ui {
class IRCamera;
}

class IRCamera : public QWidget
{
    Q_OBJECT

public:
    explicit IRCamera(QWidget *parent = nullptr);
    ~IRCamera();

    void setInputData(const QImage &irImage, const QVector<double> &temperatureMatrix, const QSize &matrixSize);
    void setStreamData(const QImage &irImage, const QVector<double> &temperatureMatrix, const QSize &matrixSize);
    void setStationEnabled(bool enabled);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onAddPointToggled(bool checked);
    void onAddBoxToggled(bool checked);
    void onDeletePoint();
    void onDeleteBox();
    void onPointTableSelectionChanged();
    void onBoxTableSelectionChanged();
    void onPointTemperatureReady(const IRCameraStation::PointResult &result);
    void onBoxTemperatureReady(const IRCameraStation::BoxResult &result);

private:
    struct PointEntry {
        int id = 0;
        QPointF pos;
        double temp = 0.0;
        QGraphicsEllipseItem *marker = nullptr;
        QGraphicsSimpleTextItem *label = nullptr;
    };

    struct BoxEntry {
        int id = 0;
        QRectF rect;
        double minTemp = 0.0;
        double maxTemp = 0.0;
        double avgTemp = 0.0;
        QGraphicsRectItem *marker = nullptr;
        QGraphicsSimpleTextItem *label = nullptr;
    };

    enum class ToolMode {
        None,
        AddPoint,
        AddBox
    };

    void ensureScene();
    void clearMeasurements();
    void updateStats();
    void updatePointTable();
    void updateBoxTable();
    void highlightSelection();
    void refreshMeasurements();
    void updatePointTemperatureById(const QString &id, double temp);
    void updateBoxTemperatureById(const QString &id, double minT, double maxT, double avgT);

    double temperatureAt(const QPointF &scenePos) const;
    QRectF clampRectToImage(const QRectF &rect) const;
    void computeBoxStats(const QRectF &rect, double &minT, double &maxT, double &avgT) const;

    void addPointAt(const QPointF &scenePos);
    void beginBoxAt(const QPointF &scenePos);
    void updateBoxPreview(const QPointF &scenePos);
    void finalizeBox(const QPointF &scenePos);

    Ui::IRCamera *ui;
    ZoomableGraphicsView *m_view = nullptr;
    QGraphicsScene *m_scene = nullptr;
    QGraphicsPixmapItem *m_pixmapItem = nullptr;

    IRCameraStationClient *m_stationClient = nullptr;
    bool m_stationEnabled = false;

    QImage m_irImage;
    QVector<double> m_temperatureMatrix;
    QSize m_matrixSize;

    QVector<PointEntry> m_points;
    QVector<BoxEntry> m_boxes;

    ToolMode m_mode = ToolMode::None;
    bool m_drawingBox = false;
    QPointF m_boxStart;
    QGraphicsRectItem *m_boxPreview = nullptr;

    int m_nextPointId = 1;
    int m_nextBoxId = 1;

    QPoint m_lastHoverPixel;
    double m_lastHoverTemp = std::numeric_limits<double>::quiet_NaN();

};

#endif // IRCAMERA_H
