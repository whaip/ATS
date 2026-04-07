#include "zoomablegraphicsview.h"

#include <QGraphicsPixmapItem>
#include <QScrollBar>
#include <QWheelEvent>
#include <QMouseEvent>

ZoomableGraphicsView::ZoomableGraphicsView(QWidget *parent)
    : QGraphicsView(parent)
{
    // 视图内部固定维护一个 scene 和一张 pixmap，用于显示实时相机画面。
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    m_pixmapItem = m_scene->addPixmap(QPixmap());
    m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);

    setBackgroundBrush(Qt::NoBrush);
    setFrameShape(QFrame::NoFrame);

    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);

    setDragMode(QGraphicsView::NoDrag);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void ZoomableGraphicsView::setImage(const QImage &image)
{
    if (image.isNull()) {
        return;
    }

    const QSize newSize = image.size();
    const bool sizeChanged = (m_lastImageSize.isValid() && m_lastImageSize != newSize);
    m_lastImageSize = newSize;

    m_pixmapItem->setPixmap(QPixmap::fromImage(image));
    m_scene->setSceneRect(m_pixmapItem->boundingRect());

    m_hasImage = true;

    // 首帧或用户未手动交互时允许自动适配窗口，连续视频帧不重置缩放和平移。
    if (!m_userInteracted) {
        m_fitOnNextResize = true;
    }
    // 如果视频源分辨率变化且用户尚未介入，也重新做一次 fit。
    if (sizeChanged && !m_userInteracted) {
        m_fitOnNextResize = true;
    }
    updateFitIfNeeded();
}

void ZoomableGraphicsView::resetView()
{
    if (!m_hasImage) {
        resetTransform();
        return;
    }

    m_fitOnNextResize = true;
    updateFitIfNeeded();
}

void ZoomableGraphicsView::setLeftDragPanningEnabled(bool enabled)
{
    m_leftDragPanning = enabled;
    if (!m_leftDragPanning && m_panning) {
        m_panning = false;
        unsetCursor();
    }
}

bool ZoomableGraphicsView::leftDragPanningEnabled() const
{
    return m_leftDragPanning;
}

void ZoomableGraphicsView::wheelEvent(QWheelEvent *event)
{
    if (!m_hasImage) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    m_userInteracted = true;

    const double zoomInFactor = 1.15;
    const double zoomOutFactor = 1.0 / zoomInFactor;

    const bool zoomIn = event->angleDelta().y() > 0;
    const double factor = zoomIn ? zoomInFactor : zoomOutFactor;

    scale(factor, factor);
    event->accept();
}

void ZoomableGraphicsView::mousePressEvent(QMouseEvent *event)
{
    if (m_leftDragPanning && event->button() == Qt::LeftButton) {
        m_panning = true;
        m_lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void ZoomableGraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_leftDragPanning && m_panning) {
        m_userInteracted = true;

        const QPoint delta = event->pos() - m_lastPanPos;
        m_lastPanPos = event->pos();

        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());

        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void ZoomableGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_leftDragPanning && event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void ZoomableGraphicsView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    updateFitIfNeeded();
}

void ZoomableGraphicsView::updateFitIfNeeded()
{
    if (!m_hasImage || !m_fitOnNextResize) {
        return;
    }

    // 只有明确需要时才 fit，避免实时刷新时不断打断用户查看细节。
    fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    m_fitOnNextResize = false;
}
