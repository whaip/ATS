#include "pcbextract.h"
#include "ui_pcbextract.h"

#include "../HDCamera/zoomablegraphicsview.h"
#include "pcb_extract.h"

#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <functional>

namespace {
cv::Mat qimageToBgrMat(const QImage &img)
{
    if (img.isNull()) {
        return {};
    }

    QImage rgb = img;
    if (rgb.format() != QImage::Format_RGB888) {
        rgb = rgb.convertToFormat(QImage::Format_RGB888);
    }

    cv::Mat rgbMat(rgb.height(), rgb.width(), CV_8UC3, const_cast<uchar *>(rgb.bits()), static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(rgbMat, bgr, cv::COLOR_RGB2BGR);
    return bgr.clone();
}

QRectF quadToRect(const std::array<cv::Point2f, 4> &quad)
{
    float minX = quad[0].x;
    float minY = quad[0].y;
    float maxX = quad[0].x;
    float maxY = quad[0].y;
    for (int i = 1; i < 4; ++i) {
        minX = std::min(minX, quad[i].x);
        minY = std::min(minY, quad[i].y);
        maxX = std::max(maxX, quad[i].x);
        maxY = std::max(maxY, quad[i].y);
    }
    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

class CropRectItem : public QGraphicsRectItem
{
public:
    enum HandleType {
        None,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        Top,
        Bottom,
        Left,
        Right
    };

    explicit CropRectItem(const QRectF &rect)
        : QGraphicsRectItem(rect)
    {
        setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges);
        setAcceptHoverEvents(true);
        setPen(QPen(QColor(0, 255, 0), 2));
    }

    void setBounds(const QRectF &bounds)
    {
        m_bounds = bounds;
        clampToBounds();
    }

    void setOnChanged(const std::function<void()> &fn)
    {
        m_onChanged = fn;
    }

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override
    {
        QGraphicsRectItem::paint(painter, option, widget);
        if (!isSelected()) {
            return;
        }

        painter->setPen(QPen(pen().color(), 1));
        painter->setBrush(Qt::white);
        QRectF r = rect();
        QRectF handle(0, 0, kHandleSize, kHandleSize);

        handle.moveCenter(r.topLeft());
        painter->drawRect(handle);
        handle.moveCenter(r.topRight());
        painter->drawRect(handle);
        handle.moveCenter(r.bottomLeft());
        painter->drawRect(handle);
        handle.moveCenter(r.bottomRight());
        painter->drawRect(handle);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_currentHandle = hitHandle(event->pos());
            m_dragStart = event->pos();
            if (m_currentHandle == None) {
                setCursor(Qt::ClosedHandCursor);
            }
        }
        QGraphicsRectItem::mousePressEvent(event);
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton)) {
            QGraphicsRectItem::mouseMoveEvent(event);
            return;
        }

        if (m_currentHandle == None) {
            QGraphicsRectItem::mouseMoveEvent(event);
            return;
        }

        const QPointF delta = event->pos() - m_dragStart;
        QRectF r = rect();
        QPointF pos = this->pos();

        switch (m_currentHandle) {
        case TopLeft:
            pos += delta;
            r.setWidth(r.width() - delta.x());
            r.setHeight(r.height() - delta.y());
            break;
        case TopRight:
            pos.setY(pos.y() + delta.y());
            r.setWidth(r.width() + delta.x());
            r.setHeight(r.height() - delta.y());
            break;
        case BottomLeft:
            pos.setX(pos.x() + delta.x());
            r.setWidth(r.width() - delta.x());
            r.setHeight(r.height() + delta.y());
            break;
        case BottomRight:
            r.setWidth(r.width() + delta.x());
            r.setHeight(r.height() + delta.y());
            break;
        case Top:
            pos.setY(pos.y() + delta.y());
            r.setHeight(r.height() - delta.y());
            break;
        case Bottom:
            r.setHeight(r.height() + delta.y());
            break;
        case Left:
            pos.setX(pos.x() + delta.x());
            r.setWidth(r.width() - delta.x());
            break;
        case Right:
            r.setWidth(r.width() + delta.x());
            break;
        default:
            break;
        }

        applyRect(pos, r.size());
        m_dragStart = event->pos();
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        unsetCursor();
        m_currentHandle = None;
        if (m_onChanged) {
            m_onChanged();
        }
        QGraphicsRectItem::mouseReleaseEvent(event);
    }

    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override
    {
        m_currentHandle = hitHandle(event->pos());
        updateCursor(m_currentHandle);
        QGraphicsRectItem::hoverMoveEvent(event);
    }

    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override
    {
        if (change == QGraphicsItem::ItemPositionChange && !m_bounds.isNull()) {
            QPointF pos = value.toPointF();
            QSizeF size = rect().size();
            pos.setX(std::clamp(pos.x(), m_bounds.left(), m_bounds.right() - size.width()));
            pos.setY(std::clamp(pos.y(), m_bounds.top(), m_bounds.bottom() - size.height()));
            return pos;
        }
        if (change == QGraphicsItem::ItemPositionHasChanged) {
            if (m_onChanged) {
                m_onChanged();
            }
        }
        return QGraphicsRectItem::itemChange(change, value);
    }

private:
    HandleType hitHandle(const QPointF &pos) const
    {
        QRectF r = rect();
        QRectF handle(0, 0, kHandleSize, kHandleSize);

        handle.moveCenter(r.topLeft());
        if (handle.contains(pos)) return TopLeft;
        handle.moveCenter(r.topRight());
        if (handle.contains(pos)) return TopRight;
        handle.moveCenter(r.bottomLeft());
        if (handle.contains(pos)) return BottomLeft;
        handle.moveCenter(r.bottomRight());
        if (handle.contains(pos)) return BottomRight;

        const qreal margin = kHandleSize / 2.0;
        if (qAbs(pos.y() - r.top()) <= margin) return Top;
        if (qAbs(pos.y() - r.bottom()) <= margin) return Bottom;
        if (qAbs(pos.x() - r.left()) <= margin) return Left;
        if (qAbs(pos.x() - r.right()) <= margin) return Right;
        return None;
    }

    void updateCursor(HandleType handle)
    {
        switch (handle) {
        case TopLeft:
        case BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case TopRight:
        case BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Top:
        case Bottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case Left:
        case Right:
            setCursor(Qt::SizeHorCursor);
            break;
        default:
            setCursor(Qt::OpenHandCursor);
            break;
        }
    }

    void applyRect(const QPointF &pos, const QSizeF &size)
    {
        QSizeF clampedSize = size;
        const qreal minW = 20.0;
        const qreal minH = 20.0;
        if (!m_bounds.isNull()) {
            clampedSize.setWidth(std::clamp(clampedSize.width(), minW, m_bounds.width()));
            clampedSize.setHeight(std::clamp(clampedSize.height(), minH, m_bounds.height()));
        } else {
            clampedSize.setWidth(std::max(clampedSize.width(), minW));
            clampedSize.setHeight(std::max(clampedSize.height(), minH));
        }

        QPointF clampedPos = pos;
        if (!m_bounds.isNull()) {
            clampedPos.setX(std::clamp(clampedPos.x(), m_bounds.left(), m_bounds.right() - clampedSize.width()));
            clampedPos.setY(std::clamp(clampedPos.y(), m_bounds.top(), m_bounds.bottom() - clampedSize.height()));
        }

        setRect(0, 0, clampedSize.width(), clampedSize.height());
        setPos(clampedPos);
        if (m_onChanged) {
            m_onChanged();
        }
    }

    void clampToBounds()
    {
        if (m_bounds.isNull()) {
            return;
        }
        applyRect(pos(), rect().size());
    }

    static constexpr qreal kHandleSize = 8.0;
    HandleType m_currentHandle = None;
    QPointF m_dragStart;
    QRectF m_bounds;
    std::function<void()> m_onChanged;
};
}

PCBExtractWidget::PCBExtractWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PCBExtractWidget)
{
    ui->setupUi(this);

    if (ui->graphicsContainer) {
        if (!ui->graphicsContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->graphicsContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_view = new ZoomableGraphicsView(ui->graphicsContainer);
        m_view->setLeftDragPanningEnabled(false);
        m_view->setDragMode(QGraphicsView::NoDrag);
        ui->graphicsContainer->layout()->addWidget(m_view);
    }

    if (ui->btnRedetect) {
        connect(ui->btnRedetect, &QPushButton::clicked, this, &PCBExtractWidget::onRedetect);
    }
    if (ui->btnConfirm) {
        connect(ui->btnConfirm, &QPushButton::clicked, this, &PCBExtractWidget::onConfirm);
    }
}

PCBExtractWidget::~PCBExtractWidget()
{
    delete ui;
}

void PCBExtractWidget::setInputImage(const QImage &image)
{
    m_inputImage = image;
    if (m_view) {
        m_view->setImage(image);
    }
    ensureSceneItems();
    onRedetect();
}

PcbExtractResult PCBExtractWidget::output() const
{
    return m_output;
}

void PCBExtractWidget::onRedetect()
{
    if (m_inputImage.isNull()) {
        return;
    }

    cv::Mat bgr = qimageToBgrMat(m_inputImage);
    if (bgr.empty()) {
        return;
    }

    cv::Mat warped;
    cv::Mat H;
    cv::Size warpedSize;
    std::array<cv::Point2f, 4> quad;
    bool ok = PCB_EXTRACT->extractWithHomography(bgr, warped, H, warpedSize, &quad);
    QRectF rect;
    if (ok) {
        rect = quadToRect(quad);
    } else {
        rect = QRectF(0, 0, m_inputImage.width(), m_inputImage.height());
    }

    applyDetectedRect(rect);
}

void PCBExtractWidget::onConfirm()
{
    updateOutput();
    emit confirmed(m_output.source, m_output.cropped, m_output.rect);
}

void PCBExtractWidget::ensureSceneItems()
{
    if (!m_view) {
        return;
    }

    m_scene = m_view->scene();
    if (!m_scene) {
        return;
    }

    if (!m_overlayItem) {
        m_overlayItem = new QGraphicsPathItem();
        m_overlayItem->setBrush(QColor(0, 0, 0, 120));
        m_overlayItem->setPen(Qt::NoPen);
        m_overlayItem->setZValue(10.0);
        m_scene->addItem(m_overlayItem);
    }

    if (!m_cropItem) {
        QRectF rect(0, 0, 100, 100);
        auto *cropItem = new CropRectItem(rect);
        cropItem->setZValue(20.0);
        cropItem->setOnChanged([this]() {
            updateOverlay();
            updateOutput();
        });
        m_scene->addItem(cropItem);
        m_cropItem = cropItem;
    }
}

void PCBExtractWidget::updateOverlay()
{
    if (!m_overlayItem || !m_cropItem || m_inputImage.isNull()) {
        return;
    }

    QRectF imageRect(0, 0, m_inputImage.width(), m_inputImage.height());
    QRectF cropRect = currentRectInScene();

    QPainterPath path;
    path.addRect(imageRect);
    path.addRect(cropRect);
    path.setFillRule(Qt::OddEvenFill);
    m_overlayItem->setPath(path);
}

void PCBExtractWidget::applyDetectedRect(const QRectF &rect)
{
    if (!m_cropItem || m_inputImage.isNull()) {
        return;
    }

    QRectF clamped = clampToImage(rect);
    auto *cropItem = static_cast<CropRectItem *>(m_cropItem);
    cropItem->setBounds(QRectF(0, 0, m_inputImage.width(), m_inputImage.height()));
    cropItem->setRect(0, 0, clamped.width(), clamped.height());
    cropItem->setPos(clamped.topLeft());
    updateOverlay();
    updateOutput();
}

QRectF PCBExtractWidget::currentRectInScene() const
{
    if (!m_cropItem) {
        return {};
    }

    QRectF r = m_cropItem->rect();
    r.moveTopLeft(m_cropItem->pos());
    return r;
}

QRectF PCBExtractWidget::clampToImage(const QRectF &rect) const
{
    if (m_inputImage.isNull()) {
        return rect;
    }

    QRectF bounds(0, 0, m_inputImage.width(), m_inputImage.height());
    QRectF r = rect;
    if (r.width() < 20.0) r.setWidth(20.0);
    if (r.height() < 20.0) r.setHeight(20.0);
    if (r.left() < bounds.left()) r.moveLeft(bounds.left());
    if (r.top() < bounds.top()) r.moveTop(bounds.top());
    if (r.right() > bounds.right()) r.moveRight(bounds.right());
    if (r.bottom() > bounds.bottom()) r.moveBottom(bounds.bottom());
    return r;
}

void PCBExtractWidget::updateOutput()
{
    if (m_inputImage.isNull()) {
        m_output = {};
        return;
    }

    QRectF rectF = clampToImage(currentRectInScene());
    QRect cropRect = rectF.toAlignedRect().intersected(QRect(0, 0, m_inputImage.width(), m_inputImage.height()));

    m_output.source = m_inputImage;
    m_output.cropped = m_inputImage.copy(cropRect);
    m_output.rect = QRectF(cropRect);
}
