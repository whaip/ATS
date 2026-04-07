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

    // 设置当前显示图像；首次加载或图像分辨率变化时会自动适配视图。
    void setImage(const QImage &image);
    // 恢复到适应窗口的初始视图。
    void resetView();
    // 是否允许按住左键拖拽平移。
    void setLeftDragPanningEnabled(bool enabled);
    bool leftDragPanningEnabled() const;

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // 在需要时执行 fitInView，但不在连续视频帧中反复重置用户缩放状态。
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
