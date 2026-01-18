#include "zoomablegraphicsview.h"

#include <QGraphicsPixmapItem>
#include <QScrollBar>
#include <QWheelEvent>
#include <QMouseEvent>

ZoomableGraphicsView::ZoomableGraphicsView(QWidget *parent)
    : QGraphicsView(parent)
{
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

    // Fit only on first image or if user hasn't interacted yet.
    // Continuous frames should not reset user's zoom/pan.
    if (!m_userInteracted) {
        m_fitOnNextResize = true;
    }
    // If the source resolution changes while user hasn't interacted, refit.
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

    fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    m_fitOnNextResize = false;
}
