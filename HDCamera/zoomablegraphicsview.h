#pragma once

#include <QObject>
#include <QGraphicsView>
#include <QImage>

class QGraphicsPixmapItem;

class ZoomableGraphicsView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit ZoomableGraphicsView(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void resetView();
    void setLeftDragPanningEnabled(bool enabled);
    bool leftDragPanningEnabled() const;

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateFitIfNeeded();

    QGraphicsScene *m_scene = nullptr;
    QGraphicsPixmapItem *m_pixmapItem = nullptr;
    bool m_hasImage = false;
    bool m_fitOnNextResize = true;
    bool m_userInteracted = false;
    QSize m_lastImageSize;

    bool m_panning = false;
    QPoint m_lastPanPos;
    bool m_leftDragPanning = true;
};
