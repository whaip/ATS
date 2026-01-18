#include "labelrectitem.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>

const qreal LabelRectItem::HandleSize = 8.0;

LabelRectItem::LabelRectItem(QGraphicsItem *parent, CompLabel label)
    : QGraphicsRectItem(parent)
    , currentHandle(None)
    , isHovered(false)
    , label_info(label)
{
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemIsFocusable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);

    updateZValue();
}

void LabelRectItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    painter->save();

    painter->setPen(rectPen);
    painter->setBrush(Qt::transparent);
    painter->drawRect(rect());

    if (isSelected()) {
        painter->setPen(QPen(rectPen.color(), 1));
        painter->setBrush(Qt::white);

        QRectF r = rect();
        QRectF handle(0, 0, HandleSize, HandleSize);

        handle.moveCenter(r.topLeft());
        painter->drawRect(handle);

        handle.moveCenter(r.topRight());
        painter->drawRect(handle);

        handle.moveCenter(r.bottomLeft());
        painter->drawRect(handle);

        handle.moveCenter(r.bottomRight());
        painter->drawRect(handle);

        if (!label_info.label.isEmpty()) {
            QRectF labelRect = getLabelRect();
            QRectF idRect = getIdRect(); 
            idRect.moveLeft(rect().left() - idRect.width());

            painter->setOpacity(1);

            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255));
            painter->drawRect(labelRect);

            painter->drawRect(idRect);

            painter->setPen(rectPen.color());
            painter->setBrush(Qt::NoBrush);
            painter->drawText(labelRect, Qt::AlignCenter, label_info.label);

            painter->drawText(idRect, Qt::AlignCenter, QString::number(label_info.id));
        }
    }

    painter->restore();
}

QRectF LabelRectItem::getIdRect() const
{
    QRectF r = rect();
    int idWidth = QString::number(label_info.id).length() * 10;
    return QRectF(r.left() - idWidth, r.top(), idWidth, r.height());
}

QRectF LabelRectItem::getLabelRect() const
{
    QRectF r = rect();
    QFont font;
    QFontMetrics fm(font);
    QString displayText = label_info.label.isEmpty() ? "Label" : label_info.label;
    int charCount = displayText.length();
    int widthPerChar = 9;
    int padding = 15;

    int labelWidth = charCount * widthPerChar + padding * 2;
    int labelHeight = fm.height() + padding;

    return QRectF(r.right() - labelWidth,
                  r.top() - labelHeight,
                  labelWidth,
                  labelHeight);
}

bool LabelRectItem::isLabelHit(const QPointF &pos) const
{
    return getLabelRect().contains(pos);
}

void LabelRectItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const HandleType clickedHandle = getHandle(event->pos());
        if (isSelected() && clickedHandle != None) {
            currentHandle = clickedHandle;
            dragStart = event->pos();
            scene()->update();
            event->accept();
            return;
        }

        currentHandle = None;
        dragStart = event->pos();
        if (isSelected()) {
            setCursor(Qt::ClosedHandCursor);
        }

        if (scene()) {
            const QList<QGraphicsItem *> selected = scene()->selectedItems();
            if (selected.size() > 1 && isSelected()) {
                groupMoveActive = true;
                groupMoveStart = event->scenePos();
                groupMoveItems.clear();
                groupMoveItems.reserve(selected.size());
                for (QGraphicsItem *item : selected) {
                    groupMoveItems.push_back(qMakePair(item, item->pos()));
                }
                event->accept();
                return;
            }
        }
    }

    QGraphicsRectItem::mousePressEvent(event);
}

void LabelRectItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (!isSelected()) {
        QGraphicsRectItem::mouseMoveEvent(event);
        return;
    }

    if (event->buttons() & Qt::LeftButton) {
        if (groupMoveActive) {
            const QPointF delta = event->scenePos() - groupMoveStart;
            for (const auto &entry : groupMoveItems) {
                if (entry.first) {
                    entry.first->setPos(entry.second + delta);
                }
            }
            event->accept();
            return;
        }

        QPointF delta = event->pos() - dragStart;
        QRectF r = rect();

        switch (currentHandle) {
        case TopLeft:
            r.setTopLeft(r.topLeft() + delta);
            break;
        case TopRight:
            r.setTopRight(r.topRight() + delta);
            break;
        case BottomLeft:
            r.setBottomLeft(r.bottomLeft() + delta);
            break;
        case BottomRight:
            r.setBottomRight(r.bottomRight() + delta);
            break;
        case Top:
            r.setTop(r.top() + delta.y());
            break;
        case Bottom:
            r.setBottom(r.bottom() + delta.y());
            break;
        case Left:
            r.setLeft(r.left() + delta.x());
            break;
        case Right:
            r.setRight(r.right() + delta.x());
            break;
        case None:
            QGraphicsRectItem::mouseMoveEvent(event);
            scene()->update();
            return;
        }

        if (r.width() >= HandleSize && r.height() >= HandleSize) {
            setRect(r);
            dragStart = event->pos();
            updateZValue();
        }
        scene()->update();
    }
}

void LabelRectItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    setCursor(Qt::ArrowCursor);
    groupMoveActive = false;
    groupMoveItems.clear();
    QGraphicsRectItem::mouseReleaseEvent(event);
}

void LabelRectItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    if (!isSelected()) {
        setCursor(Qt::ArrowCursor);
        currentHandle = None;
        return;
    }

    // ???????????? handle
    currentHandle = getHandle(event->pos());
    updateCursor(currentHandle);
    QGraphicsRectItem::hoverMoveEvent(event);
}

LabelRectItem::HandleType LabelRectItem::getHandle(const QPointF &pos) const
{
    QRectF r = rect();
    QRectF handle(0, 0, HandleSize, HandleSize);

    handle.moveCenter(r.topLeft());
    if (handle.contains(pos)) return TopLeft;

    handle.moveCenter(r.topRight());
    if (handle.contains(pos)) return TopRight;

    handle.moveCenter(r.bottomLeft());
    if (handle.contains(pos)) return BottomLeft;

    handle.moveCenter(r.bottomRight());
    if (handle.contains(pos)) return BottomRight;

    const qreal margin = HandleSize / 2;
    if (qAbs(pos.y() - r.top()) <= margin && pos.x() > r.left() + margin && pos.x() < r.right() - margin)
        return Top;
    if (qAbs(pos.y() - r.bottom()) <= margin && pos.x() > r.left() + margin && pos.x() < r.right() - margin)
        return Bottom;
    if (qAbs(pos.x() - r.left()) <= margin && pos.y() > r.top() + margin && pos.y() < r.bottom() - margin)
        return Left;
    if (qAbs(pos.x() - r.right()) <= margin && pos.y() > r.top() + margin && pos.y() < r.bottom() - margin)
        return Right;
    return None;
}

void LabelRectItem::updateCursor(HandleType handle)
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

QRectF LabelRectItem::sceneRect() const
{
    return mapToScene(rect()).boundingRect();
}

void LabelRectItem::keyPressEvent(QKeyEvent *event)
{
    if (!isSelected() || !isHovered || !(event->modifiers() & Qt::ShiftModifier)) {
        QGraphicsRectItem::keyPressEvent(event);
        return;
    }

    const qreal step = 1.0;
    QRectF r = rect();
    QPointF pos = this->pos();

    switch (event->key()) {
    case Qt::Key_Left:
        if (currentHandle == None) {
            setPos(pos.x() - step, pos.y());
        } else {
            switch (currentHandle) {
            case Left:
            case TopLeft:
            case BottomLeft:
                r.setLeft(r.left() - step);
                break;
            case Right:
            case TopRight:
            case BottomRight:
                r.setRight(r.right() - step);
                break;
            default:
                break;
            }
        }
        break;

    case Qt::Key_Right:
        if (currentHandle == None) {
            setPos(pos.x() + step, pos.y());
        } else {
            switch (currentHandle) {
            case Left:
            case TopLeft:
            case BottomLeft:
                r.setLeft(r.left() + step);
                break;
            case Right:
            case TopRight:
            case BottomRight:
                r.setRight(r.right() + step);
                break;
            default:
                break;
            }
        }
        break;

    case Qt::Key_Up:
        if (currentHandle == None) {
            setPos(pos.x(), pos.y() - step);
        } else {
            switch (currentHandle) {
            case Top:
            case TopLeft:
            case TopRight:
                r.setTop(r.top() - step);
                break;
            case Bottom:
            case BottomLeft:
            case BottomRight:
                r.setBottom(r.bottom() - step);
                break;
            default:
                break;
            }
        }
        break;

    case Qt::Key_Down:
        if (currentHandle == None) {
            setPos(pos.x(), pos.y() + step);
        } else {
            switch (currentHandle) {
            case Top:
            case TopLeft:
            case TopRight:
                r.setTop(r.top() + step);
                break;
            case Bottom:
            case BottomLeft:
            case BottomRight:
                r.setBottom(r.bottom() + step);
                break;
            default:
                break;
            }
        }
        break;

    default:
        QGraphicsRectItem::keyPressEvent(event);
        return;
    }

    if (r.width() >= HandleSize && r.height() >= HandleSize) {
        setRect(r);
    }
    scene()->update();
    event->accept();
}

void LabelRectItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    isHovered = true;
    setFocus(); 
    QGraphicsRectItem::hoverEnterEvent(event);
}

void LabelRectItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    isHovered = false;
    currentHandle = None; 
    clearFocus();
    setCursor(Qt::ArrowCursor);
    QGraphicsRectItem::hoverLeaveEvent(event);
}

void LabelRectItem::updateZValue()
{
    QRectF r = rect();
    qreal area = r.width() * r.height();
    if (area > 0) {
        setZValue(100000.0 / area);
    }
}

void LabelRectItem::setRect(const QRectF &rect)
{
    QGraphicsRectItem::setRect(rect);
    updateZValue();

    label_info.x = rect.x();
    label_info.y = rect.y();
    label_info.w   = rect.width();
    label_info.h  = rect.height();

    label_info.x = rect.x();
    label_info.y = rect.y();
    label_info.w = rect.width();
    label_info.h = rect.height();

    update();
}

QVariant LabelRectItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == QGraphicsItem::ItemSelectedHasChanged) {
        update();
    }

    if (change == QGraphicsItem::ItemPositionChange || change == QGraphicsItem::ItemScaleChange) {
        updateZValue();
        
        QRectF r = sceneRect();
        
        if (scene() && !scene()->sceneRect().isEmpty()) {
            QRectF sceneRect = scene()->sceneRect();

            if (r.x() >= 0 && r.y() >= 0 && 
                r.right() <= sceneRect.width() && r.bottom() <= sceneRect.height()) {
                
                label_info.x = r.x();
                label_info.y = r.y();
                label_info.w = r.width();
                label_info.h = r.height();
                
            } else {
                // qDebug() << "LabelRectItem::itemChange - 坐标更新ID:" << label_info.id 
                //          << "位置:" << r.x() << "," << r.y() << r.width() << "x" << r.height()
                //          << "场景范围:" << sceneRect;
            }
        } else {
            // qDebug() << "LabelRectItem::itemChange - 场景无效，忽略坐标更新，ID:" << label_info.id;
        }
    }
    return QGraphicsRectItem::itemChange(change, value);
}

LabelRectItem::HandleType LabelRectItem::handleAt(const QPointF &pos) const
{
    return getHandle(pos);
}
