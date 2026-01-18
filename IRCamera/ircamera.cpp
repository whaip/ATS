#include "ircamera.h"
#include "ui_ircamera.h"

#include "ircamerastationclient.h"

#include "../HDCamera/zoomablegraphicsview.h"

#include <QEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QToolTip>
#include <QCursor>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QColor pointColor()
{
    return QColor(255, 80, 80, 200);
}

QColor boxColor()
{
    return QColor(0, 200, 255, 200);
}

QString formatTemp(double temp)
{
    if (std::isnan(temp)) {
        return QStringLiteral("-");
    }
    return QStringLiteral("%1 °C").arg(temp, 0, 'f', 2);
}
}

IRCamera::IRCamera(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::IRCamera)
{
    ui->setupUi(this);

    if (ui->graphicsContainer) {
        if (!ui->graphicsContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->graphicsContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_view = new ZoomableGraphicsView(ui->graphicsContainer);
        m_view->setLeftDragPanningEnabled(true);
        m_view->setDragMode(QGraphicsView::NoDrag);
        ui->graphicsContainer->layout()->addWidget(m_view);
        m_view->viewport()->installEventFilter(this);
    }

    ensureScene();

    if (ui->btnAddPoint) {
        ui->btnAddPoint->setCheckable(true);
        connect(ui->btnAddPoint, &QPushButton::toggled, this, &IRCamera::onAddPointToggled);
    }
    if (ui->btnAddBox) {
        ui->btnAddBox->setCheckable(true);
        connect(ui->btnAddBox, &QPushButton::toggled, this, &IRCamera::onAddBoxToggled);
    }
    if (ui->btnDeletePoint) {
        connect(ui->btnDeletePoint, &QPushButton::clicked, this, &IRCamera::onDeletePoint);
    }
    if (ui->btnDeleteBox) {
        connect(ui->btnDeleteBox, &QPushButton::clicked, this, &IRCamera::onDeleteBox);
    }

    if (ui->tablePoints) {
        connect(ui->tablePoints, &QTableWidget::itemSelectionChanged, this, &IRCamera::onPointTableSelectionChanged);
    }
    if (ui->tableBoxes) {
        connect(ui->tableBoxes, &QTableWidget::itemSelectionChanged, this, &IRCamera::onBoxTableSelectionChanged);
    }

    updateStats();
}

IRCamera::~IRCamera()
{
    if (m_stationClient) {
        m_stationClient->clearSubscriptions();
    }
    delete ui;
}

void IRCamera::setInputData(const QImage &irImage, const QVector<double> &temperatureMatrix, const QSize &matrixSize)
{
    m_irImage = irImage;
    m_temperatureMatrix = temperatureMatrix;
    m_matrixSize = matrixSize;

    if (m_view) {
        m_view->setImage(irImage);
    }

    ensureScene();
    clearMeasurements();
    updateStats();
}

void IRCamera::setStreamData(const QImage &irImage, const QVector<double> &temperatureMatrix, const QSize &matrixSize)
{
    m_irImage = irImage;
    m_temperatureMatrix = temperatureMatrix;
    m_matrixSize = matrixSize;

    if (m_view) {
        m_view->setImage(irImage);
    }

    ensureScene();
    updateStats();
    if (!m_stationEnabled) {
        refreshMeasurements();
    }
}

void IRCamera::setStationEnabled(bool enabled)
{
    m_stationEnabled = enabled;
    if (!m_stationClient) {
        m_stationClient = new IRCameraStationClient(QStringLiteral("IRCamera"), this);
        connect(m_stationClient, &IRCameraStationClient::frameReady, this,
                [this](const IRCameraStation::Frame &frame) {
                    setStreamData(frame.irImage, frame.temperatureMatrix, frame.matrixSize);
                },
                Qt::QueuedConnection);
        connect(m_stationClient, &IRCameraStationClient::pointTemperatureReady, this,
            &IRCamera::onPointTemperatureReady, Qt::QueuedConnection);
        connect(m_stationClient, &IRCameraStationClient::boxTemperatureReady, this,
            &IRCamera::onBoxTemperatureReady, Qt::QueuedConnection);
    }

    m_stationClient->setEnabled(enabled);

    if (enabled) {
        IRCameraStation::Frame frame;
        if (m_stationClient->tryGetLatestFrame(&frame)) {
            setStreamData(frame.irImage, frame.temperatureMatrix, frame.matrixSize);
        }
    }
}

bool IRCamera::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_view || watched != m_view->viewport() || !event) {
        return QWidget::eventFilter(watched, event);
    }

    if (m_irImage.isNull()) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            const QPointF scenePos = m_view->mapToScene(me->pos());
            if (m_mode == ToolMode::AddPoint) {
                addPointAt(scenePos);
                return true;
            }
            if (m_mode == ToolMode::AddBox) {
                beginBoxAt(scenePos);
                return true;
            }
        }
    }

    if (event->type() == QEvent::MouseMove && m_mode == ToolMode::AddBox && m_drawingBox) {
        auto *me = static_cast<QMouseEvent *>(event);
        updateBoxPreview(m_view->mapToScene(me->pos()));
        return true;
    }

    if (event->type() == QEvent::MouseMove && !m_drawingBox) {
        auto *me = static_cast<QMouseEvent *>(event);
        const QPointF scenePos = m_view->mapToScene(me->pos());
        const double temp = temperatureAt(scenePos);

        if (!m_irImage.isNull()) {
            const int px = std::clamp(static_cast<int>(scenePos.x()), 0, m_irImage.width() - 1);
            const int py = std::clamp(static_cast<int>(scenePos.y()), 0, m_irImage.height() - 1);
            const QPoint pixel(px, py);
            if (pixel != m_lastHoverPixel || temp != m_lastHoverTemp) {
                m_lastHoverPixel = pixel;
                m_lastHoverTemp = temp;
                const QString tip = QStringLiteral("(%1, %2) %3").arg(px).arg(py).arg(formatTemp(temp));
                QToolTip::showText(QCursor::pos(), tip, m_view);
            }
        }
    }

    if (event->type() == QEvent::MouseButtonRelease && m_mode == ToolMode::AddBox && m_drawingBox) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            finalizeBox(m_view->mapToScene(me->pos()));
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void IRCamera::closeEvent(QCloseEvent *event)
{
    if (m_stationClient) {
        m_stationClient->clearSubscriptions();
    }
    m_stationEnabled = false;
    if (m_stationClient) {
        m_stationClient->setEnabled(false);
    }
    QWidget::closeEvent(event);
}

void IRCamera::onAddPointToggled(bool checked)
{
    if (checked) {
        m_mode = ToolMode::AddPoint;
        if (ui->btnAddBox) {
            ui->btnAddBox->setChecked(false);
        }
        if (m_view) {
            m_view->setLeftDragPanningEnabled(false);
            m_view->viewport()->setCursor(Qt::PointingHandCursor);
        }
    } else if (m_mode == ToolMode::AddPoint) {
        m_mode = ToolMode::None;
        if (m_view) {
            m_view->setLeftDragPanningEnabled(true);
            m_view->viewport()->unsetCursor();
        }
    }
}

void IRCamera::onAddBoxToggled(bool checked)
{
    if (checked) {
        m_mode = ToolMode::AddBox;
        if (ui->btnAddPoint) {
            ui->btnAddPoint->setChecked(false);
        }
        if (m_view) {
            m_view->setLeftDragPanningEnabled(false);
            m_view->viewport()->setCursor(Qt::CrossCursor);
        }
    } else if (m_mode == ToolMode::AddBox) {
        m_mode = ToolMode::None;
        if (m_view) {
            m_view->setLeftDragPanningEnabled(true);
            m_view->viewport()->unsetCursor();
        }
    }
}

void IRCamera::onDeletePoint()
{
    if (!ui->tablePoints || m_points.isEmpty()) {
        return;
    }

    const QList<QTableWidgetSelectionRange> ranges = ui->tablePoints->selectedRanges();
    if (ranges.isEmpty()) {
        return;
    }

    QVector<int> rows;
    for (const auto &range : ranges) {
        for (int r = range.topRow(); r <= range.bottomRow(); ++r) {
            rows.append(r);
        }
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    for (int row : rows) {
        if (row < 0 || row >= m_points.size()) {
            continue;
        }
        PointEntry entry = m_points.takeAt(row);
        if (m_stationClient && m_stationEnabled) {
            m_stationClient->unsubscribePoint(QString::number(entry.id));
        }
        if (m_scene) {
            if (entry.marker) m_scene->removeItem(entry.marker);
            if (entry.label) m_scene->removeItem(entry.label);
        }
        delete entry.marker;
        delete entry.label;
    }

    updatePointTable();
    highlightSelection();
}

void IRCamera::onDeleteBox()
{
    if (!ui->tableBoxes || m_boxes.isEmpty()) {
        return;
    }

    const QList<QTableWidgetSelectionRange> ranges = ui->tableBoxes->selectedRanges();
    if (ranges.isEmpty()) {
        return;
    }

    QVector<int> rows;
    for (const auto &range : ranges) {
        for (int r = range.topRow(); r <= range.bottomRow(); ++r) {
            rows.append(r);
        }
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    for (int row : rows) {
        if (row < 0 || row >= m_boxes.size()) {
            continue;
        }
        BoxEntry entry = m_boxes.takeAt(row);
        if (m_stationClient && m_stationEnabled) {
            m_stationClient->unsubscribeBox(QString::number(entry.id));
        }
        if (m_scene) {
            if (entry.marker) m_scene->removeItem(entry.marker);
            if (entry.label) m_scene->removeItem(entry.label);
        }
        delete entry.marker;
        delete entry.label;
    }

    updateBoxTable();
    highlightSelection();
}

void IRCamera::onPointTableSelectionChanged()
{
    highlightSelection();
}

void IRCamera::onBoxTableSelectionChanged()
{
    highlightSelection();
}

void IRCamera::ensureScene()
{
    if (!m_view) {
        return;
    }

    m_scene = m_view->scene();
    if (!m_scene) {
        return;
    }

    QGraphicsPixmapItem *pixmapItem = nullptr;
    const auto items = m_scene->items();
    for (QGraphicsItem *item : items) {
        if (auto *pix = qgraphicsitem_cast<QGraphicsPixmapItem *>(item)) {
            pixmapItem = pix;
            break;
        }
    }
    m_pixmapItem = pixmapItem;
}

void IRCamera::clearMeasurements()
{
    if (m_scene) {
        for (const auto &p : m_points) {
            if (p.marker) m_scene->removeItem(p.marker);
            if (p.label) m_scene->removeItem(p.label);
            delete p.marker;
            delete p.label;
        }
        for (const auto &b : m_boxes) {
            if (b.marker) m_scene->removeItem(b.marker);
            if (b.label) m_scene->removeItem(b.label);
            delete b.marker;
            delete b.label;
        }
    }

    m_points.clear();
    m_boxes.clear();
    m_nextPointId = 1;
    m_nextBoxId = 1;
    updatePointTable();
    updateBoxTable();

    if (m_stationClient && m_stationEnabled) {
        m_stationClient->clearSubscriptions();
    }
}

void IRCamera::updateStats()
{
    if (!ui->tableStats) {
        return;
    }

    ui->tableStats->setRowCount(3);
    ui->tableStats->setColumnCount(2);
    ui->tableStats->setHorizontalHeaderLabels({QStringLiteral("指标"), QStringLiteral("数值")});

    double minT = std::numeric_limits<double>::infinity();
    double maxT = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
    int count = 0;

    if (!m_temperatureMatrix.isEmpty()) {
        for (double v : m_temperatureMatrix) {
            minT = std::min(minT, v);
            maxT = std::max(maxT, v);
            sum += v;
            ++count;
        }
    }

    const bool hasData = (count > 0);
    const double avg = hasData ? (sum / count) : std::numeric_limits<double>::quiet_NaN();

    ui->tableStats->setItem(0, 0, new QTableWidgetItem(QStringLiteral("最高温度")));
    ui->tableStats->setItem(0, 1, new QTableWidgetItem(formatTemp(hasData ? maxT : std::numeric_limits<double>::quiet_NaN())));
    ui->tableStats->setItem(1, 0, new QTableWidgetItem(QStringLiteral("最低温度")));
    ui->tableStats->setItem(1, 1, new QTableWidgetItem(formatTemp(hasData ? minT : std::numeric_limits<double>::quiet_NaN())));
    ui->tableStats->setItem(2, 0, new QTableWidgetItem(QStringLiteral("平均温度")));
    ui->tableStats->setItem(2, 1, new QTableWidgetItem(formatTemp(avg)));

    ui->tableStats->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableStats->setSelectionMode(QAbstractItemView::NoSelection);
    ui->tableStats->horizontalHeader()->setStretchLastSection(true);
}

void IRCamera::updatePointTable()
{
    if (!ui->tablePoints) {
        return;
    }

    ui->tablePoints->setRowCount(m_points.size());
    for (int i = 0; i < m_points.size(); ++i) {
        const auto &p = m_points[i];
        ui->tablePoints->setItem(i, 0, new QTableWidgetItem(QString::number(p.id)));
        ui->tablePoints->setItem(i, 1, new QTableWidgetItem(QString::number(p.pos.x(), 'f', 1)));
        ui->tablePoints->setItem(i, 2, new QTableWidgetItem(QString::number(p.pos.y(), 'f', 1)));
        ui->tablePoints->setItem(i, 3, new QTableWidgetItem(formatTemp(p.temp)));
    }
    ui->tablePoints->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tablePoints->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tablePoints->horizontalHeader()->setStretchLastSection(true);
}

void IRCamera::updateBoxTable()
{
    if (!ui->tableBoxes) {
        return;
    }

    ui->tableBoxes->setRowCount(m_boxes.size());
    for (int i = 0; i < m_boxes.size(); ++i) {
        const auto &b = m_boxes[i];
        ui->tableBoxes->setItem(i, 0, new QTableWidgetItem(QString::number(b.id)));
        ui->tableBoxes->setItem(i, 1, new QTableWidgetItem(QString::number(b.rect.x(), 'f', 1)));
        ui->tableBoxes->setItem(i, 2, new QTableWidgetItem(QString::number(b.rect.y(), 'f', 1)));
        ui->tableBoxes->setItem(i, 3, new QTableWidgetItem(QString::number(b.rect.width(), 'f', 1)));
        ui->tableBoxes->setItem(i, 4, new QTableWidgetItem(QString::number(b.rect.height(), 'f', 1)));
        ui->tableBoxes->setItem(i, 5, new QTableWidgetItem(formatTemp(b.minTemp)));
        ui->tableBoxes->setItem(i, 6, new QTableWidgetItem(formatTemp(b.maxTemp)));
        ui->tableBoxes->setItem(i, 7, new QTableWidgetItem(formatTemp(b.avgTemp)));
    }
    ui->tableBoxes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableBoxes->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableBoxes->horizontalHeader()->setStretchLastSection(true);
}

void IRCamera::highlightSelection()
{
    for (auto &p : m_points) {
        if (p.marker) {
            QPen pen(pointColor());
            pen.setWidth(2);
            p.marker->setPen(pen);
        }
    }
    for (auto &b : m_boxes) {
        if (b.marker) {
            QPen pen(boxColor());
            pen.setWidth(2);
            b.marker->setPen(pen);
        }
    }

    if (ui->tablePoints) {
        const auto ranges = ui->tablePoints->selectedRanges();
        for (const auto &range : ranges) {
            for (int r = range.topRow(); r <= range.bottomRow(); ++r) {
                if (r >= 0 && r < m_points.size() && m_points[r].marker) {
                    QPen pen(pointColor());
                    pen.setWidth(3);
                    m_points[r].marker->setPen(pen);
                }
            }
        }
    }

    if (ui->tableBoxes) {
        const auto ranges = ui->tableBoxes->selectedRanges();
        for (const auto &range : ranges) {
            for (int r = range.topRow(); r <= range.bottomRow(); ++r) {
                if (r >= 0 && r < m_boxes.size() && m_boxes[r].marker) {
                    QPen pen(boxColor());
                    pen.setWidth(3);
                    m_boxes[r].marker->setPen(pen);
                }
            }
        }
    }
}

void IRCamera::onPointTemperatureReady(const IRCameraStation::PointResult &result)
{
    updatePointTemperatureById(result.id, result.temp);
}

void IRCamera::onBoxTemperatureReady(const IRCameraStation::BoxResult &result)
{
    updateBoxTemperatureById(result.id, result.minTemp, result.maxTemp, result.avgTemp);
}

void IRCamera::updatePointTemperatureById(const QString &id, double temp)
{
    bool ok = false;
    const int targetId = id.toInt(&ok);
    if (!ok) {
        return;
    }

    for (int i = 0; i < m_points.size(); ++i) {
        auto &entry = m_points[i];
        if (entry.id == targetId) {
            entry.temp = temp;
            if (ui->tablePoints) {
                ui->tablePoints->setItem(i, 3, new QTableWidgetItem(formatTemp(entry.temp)));
            }
            break;
        }
    }
}

void IRCamera::updateBoxTemperatureById(const QString &id, double minT, double maxT, double avgT)
{
    bool ok = false;
    const int targetId = id.toInt(&ok);
    if (!ok) {
        return;
    }

    for (int i = 0; i < m_boxes.size(); ++i) {
        auto &entry = m_boxes[i];
        if (entry.id == targetId) {
            entry.minTemp = minT;
            entry.maxTemp = maxT;
            entry.avgTemp = avgT;
            if (ui->tableBoxes) {
                ui->tableBoxes->setItem(i, 5, new QTableWidgetItem(formatTemp(entry.minTemp)));
                ui->tableBoxes->setItem(i, 6, new QTableWidgetItem(formatTemp(entry.maxTemp)));
                ui->tableBoxes->setItem(i, 7, new QTableWidgetItem(formatTemp(entry.avgTemp)));
            }
            break;
        }
    }
}

void IRCamera::refreshMeasurements()
{
    for (auto &p : m_points) {
        p.temp = temperatureAt(p.pos);
    }
    for (auto &b : m_boxes) {
        computeBoxStats(b.rect, b.minTemp, b.maxTemp, b.avgTemp);
    }

    updateStats();
    updatePointTable();
    updateBoxTable();
    highlightSelection();
}

double IRCamera::temperatureAt(const QPointF &scenePos) const
{
    if (m_temperatureMatrix.isEmpty() || !m_matrixSize.isValid() || m_irImage.isNull()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double imgW = m_irImage.width();
    const double imgH = m_irImage.height();
    if (imgW <= 0.0 || imgH <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    int tx = static_cast<int>(scenePos.x() / imgW * m_matrixSize.width());
    int ty = static_cast<int>(scenePos.y() / imgH * m_matrixSize.height());
    tx = std::clamp(tx, 0, m_matrixSize.width() - 1);
    ty = std::clamp(ty, 0, m_matrixSize.height() - 1);

    const int idx = ty * m_matrixSize.width() + tx;
    if (idx < 0 || idx >= m_temperatureMatrix.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return m_temperatureMatrix[idx];
}

QRectF IRCamera::clampRectToImage(const QRectF &rect) const
{
    if (m_irImage.isNull()) {
        return rect;
    }
    QRectF bounds(0, 0, m_irImage.width(), m_irImage.height());
    QRectF r = rect.normalized();
    r.setLeft(std::clamp(r.left(), bounds.left(), bounds.right()));
    r.setTop(std::clamp(r.top(), bounds.top(), bounds.bottom()));
    r.setRight(std::clamp(r.right(), bounds.left(), bounds.right()));
    r.setBottom(std::clamp(r.bottom(), bounds.top(), bounds.bottom()));
    return r;
}

void IRCamera::computeBoxStats(const QRectF &rect, double &minT, double &maxT, double &avgT) const
{
    minT = std::numeric_limits<double>::infinity();
    maxT = -std::numeric_limits<double>::infinity();
    avgT = std::numeric_limits<double>::quiet_NaN();

    if (m_temperatureMatrix.isEmpty() || !m_matrixSize.isValid() || m_irImage.isNull()) {
        return;
    }

    QRectF r = clampRectToImage(rect);
    if (r.width() <= 0.0 || r.height() <= 0.0) {
        return;
    }

    const int x0 = static_cast<int>(r.left() / m_irImage.width() * m_matrixSize.width());
    const int y0 = static_cast<int>(r.top() / m_irImage.height() * m_matrixSize.height());
    const int x1 = static_cast<int>(r.right() / m_irImage.width() * m_matrixSize.width());
    const int y1 = static_cast<int>(r.bottom() / m_irImage.height() * m_matrixSize.height());

    const int xmin = std::clamp(std::min(x0, x1), 0, m_matrixSize.width() - 1);
    const int xmax = std::clamp(std::max(x0, x1), 0, m_matrixSize.width() - 1);
    const int ymin = std::clamp(std::min(y0, y1), 0, m_matrixSize.height() - 1);
    const int ymax = std::clamp(std::max(y0, y1), 0, m_matrixSize.height() - 1);

    double sum = 0.0;
    int count = 0;
    for (int y = ymin; y <= ymax; ++y) {
        const int row = y * m_matrixSize.width();
        for (int x = xmin; x <= xmax; ++x) {
            const double v = m_temperatureMatrix[row + x];
            minT = std::min(minT, v);
            maxT = std::max(maxT, v);
            sum += v;
            ++count;
        }
    }

    if (count > 0) {
        avgT = sum / count;
    }
}

void IRCamera::addPointAt(const QPointF &scenePos)
{
    if (!m_scene || m_irImage.isNull()) {
        return;
    }

    const QRectF bounds(0, 0, m_irImage.width(), m_irImage.height());
    if (!bounds.contains(scenePos)) {
        return;
    }

    PointEntry entry;
    entry.id = m_nextPointId++;
    entry.pos = scenePos;
    entry.temp = temperatureAt(scenePos);

    const QColor color = pointColor();
    const qreal radius = 4.0;
    entry.marker = m_scene->addEllipse(scenePos.x() - radius, scenePos.y() - radius, radius * 2.0, radius * 2.0,
                                       QPen(color, 2), QBrush(QColor(color.red(), color.green(), color.blue(), 120)));
    entry.marker->setZValue(10.0);

    entry.label = m_scene->addSimpleText(QStringLiteral("P%1").arg(entry.id));
    entry.label->setBrush(QBrush(QColor(255, 255, 255, 180)));
    entry.label->setPos(scenePos.x() + 6, scenePos.y() - 10);
    entry.label->setZValue(11.0);

    m_points.push_back(entry);
    updatePointTable();
    highlightSelection();

    if (m_stationClient && m_stationEnabled) {
        m_stationClient->subscribePoint(QString::number(entry.id), entry.pos);
    }
}

void IRCamera::beginBoxAt(const QPointF &scenePos)
{
    if (!m_scene || m_irImage.isNull()) {
        return;
    }

    const QRectF bounds(0, 0, m_irImage.width(), m_irImage.height());
    if (!bounds.contains(scenePos)) {
        return;
    }

    m_drawingBox = true;
    m_boxStart = scenePos;

    if (!m_boxPreview) {
        QPen pen(boxColor());
        pen.setWidth(2);
        m_boxPreview = m_scene->addRect(QRectF(scenePos, QSizeF(1, 1)), pen, Qt::NoBrush);
        m_boxPreview->setZValue(9.0);
    }
}

void IRCamera::updateBoxPreview(const QPointF &scenePos)
{
    if (!m_boxPreview) {
        return;
    }
    QRectF rect(m_boxStart, scenePos);
    rect = clampRectToImage(rect);
    m_boxPreview->setRect(rect.normalized());
}

void IRCamera::finalizeBox(const QPointF &scenePos)
{
    if (!m_scene || !m_boxPreview) {
        m_drawingBox = false;
        return;
    }

    QRectF rect(m_boxStart, scenePos);
    rect = clampRectToImage(rect);
    rect = rect.normalized();

    m_scene->removeItem(m_boxPreview);
    delete m_boxPreview;
    m_boxPreview = nullptr;
    m_drawingBox = false;

    if (rect.width() < 2.0 || rect.height() < 2.0) {
        return;
    }

    BoxEntry entry;
    entry.id = m_nextBoxId++;
    entry.rect = rect;
    computeBoxStats(rect, entry.minTemp, entry.maxTemp, entry.avgTemp);

    QPen pen(boxColor());
    pen.setWidth(2);
    entry.marker = m_scene->addRect(rect, pen, Qt::NoBrush);
    entry.marker->setZValue(9.0);

    entry.label = m_scene->addSimpleText(QStringLiteral("R%1").arg(entry.id));
    entry.label->setBrush(QBrush(QColor(255, 255, 255, 180)));
    entry.label->setPos(rect.topLeft() + QPointF(4, -16));
    entry.label->setZValue(11.0);

    m_boxes.push_back(entry);
    updateBoxTable();
    highlightSelection();

    if (m_stationClient && m_stationEnabled) {
        m_stationClient->subscribeBox(QString::number(entry.id), entry.rect);
    }
}
