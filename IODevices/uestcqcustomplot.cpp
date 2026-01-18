#include "uestcqcustomplot.h"

#include <QtWidgets/QToolButton>
#include <QtGui/QMouseEvent>
#include <QtGui/QResizeEvent>
#include <QtCore/QEvent>
#include <QtCore/QTimer>

#include <cmath>
#include <limits>

namespace {
class UESTCTimeTicker : public QCPAxisTickerDateTime
{
public:
	UESTCTimeTicker()
	{
		setDateTimeFormat(QStringLiteral("MM/dd hh:mm:ss.zzz"));
	}

protected:
	QString getTickLabel(double tick, const QLocale &locale, QChar formatChar, int precision) override
	{
		Q_UNUSED(formatChar)
		Q_UNUSED(precision)
		const qint64 micros = static_cast<qint64>(std::llround(tick * 1000000.0));
		const qint64 millis = micros / 1000;
		const int microRemainder = static_cast<int>(micros % 1000);
		const QDateTime dt = QDateTime::fromMSecsSinceEpoch(millis).toLocalTime();
		const QString base = locale.toString(dt, dateTimeFormat());
		return QStringLiteral("%1.%2")
			.arg(base)
			.arg(microRemainder, 3, 10, QLatin1Char('0'));
	}
};

QColor pickColor(int index)
{
	static const QVector<QColor> palette = {
		QColor(31, 119, 180), QColor(255, 127, 14), QColor(44, 160, 44),
		QColor(214, 39, 40),  QColor(148, 103, 189), QColor(140, 86, 75),
		QColor(227, 119, 194), QColor(127, 127, 127), QColor(188, 189, 34),
		QColor(23, 190, 207)
	};
	return palette[index % palette.size()];
}
}

UESTCQCustomPlot::UESTCQCustomPlot(QWidget *parent)
	: QCustomPlot(parent)
{
	setupPlot();
}

void UESTCQCustomPlot::setupPlot()
{
	m_timeTicker.reset(new UESTCTimeTicker());

	setNoAntialiasingOnDrag(true);
	setNotAntialiasedElements(QCP::aeAll);
	setAntialiasedElements(QCP::aePlottables);
	setPlottingHint(QCP::phFastPolylines, true);
	setBufferDevicePixelRatio(1);
	setOpenGl(true);
	setPlottingHints(QCP::phFastPolylines);
	setAntialiasedElements(QCP::aeNone);
	setNotAntialiasedElements(QCP::aeAll);

	legend->setVisible(true);
	legend->setSelectableParts(QCPLegend::spNone);
	legend->setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));

	xAxis->setTicker(m_timeTicker);
	xAxis->setLabel(QStringLiteral("时间"));
	yAxis->setLabel(QStringLiteral("数值"));
	xAxis->setNumberFormat("f");
	xAxis->setNumberPrecision(3);

	setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
	axisRect()->setupFullAxesBox(true);
	axisRect()->setRangeZoomAxes(xAxis, yAxis);
	axisRect()->setRangeDragAxes(xAxis, yAxis);

	m_tracer = new QCPItemTracer(this);
	m_tracer->setStyle(QCPItemTracer::tsCircle);
	m_tracer->setSize(7);
	m_tracer->setVisible(false);

	m_tracerLabel = new QCPItemText(this);
	m_tracerLabel->setPadding(QMargins(6, 4, 6, 4));
	m_tracerLabel->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	m_tracerLabel->setVisible(false);

	m_tracerLine = new QCPItemLine(this);
	m_tracerLine->setVisible(false);

	m_btnLegendCollapse = new QToolButton(this);
	m_btnLegendCollapse->setText(QStringLiteral("图例折叠"));
	m_btnLegendCollapse->setAutoRaise(true);

	m_btnLegendExpand = new QToolButton(this);
	m_btnLegendExpand->setText(QStringLiteral("图例展开"));
	m_btnLegendExpand->setAutoRaise(true);
	m_btnLegendExpand->setVisible(false);

	connect(m_btnLegendCollapse, &QToolButton::clicked, this, [this]() { toggleLegend(false); });
	connect(m_btnLegendExpand, &QToolButton::clicked, this, [this]() { toggleLegend(true); });
	connect(this, &QCustomPlot::legendClick, this, &UESTCQCustomPlot::handleLegendClick);
	connect(this, &QCustomPlot::mouseMove, this, &UESTCQCustomPlot::handleMouseMove);
	connect(this, &QCustomPlot::mouseDoubleClick, this, &UESTCQCustomPlot::handleMouseDoubleClick);

	m_autoResumeTimer = new QTimer(this);
	m_autoResumeTimer->setSingleShot(true);
	connect(m_autoResumeTimer, &QTimer::timeout, this, &UESTCQCustomPlot::resumeAutoRange);

	applyTheme();
}

void UESTCQCustomPlot::resizeEvent(QResizeEvent *event)
{
	QCustomPlot::resizeEvent(event);
	refreshLegendButtonPosition();
}

void UESTCQCustomPlot::changeEvent(QEvent *event)
{
	if (event && event->type() == QEvent::PaletteChange) {
		applyTheme();
	}
	QCustomPlot::changeEvent(event);
}

void UESTCQCustomPlot::applyTheme()
{
	const QPalette pal = palette();
	const QColor windowColor = pal.color(QPalette::Window);
	const QColor baseColor = pal.color(QPalette::Base);
	const QColor textColor = pal.color(QPalette::Text);
	const QColor midColor = pal.color(QPalette::Mid);
	const QColor midLightColor = pal.color(QPalette::Midlight);
	const QColor highlightColor = pal.color(QPalette::Highlight);
	const QColor tooltipBase = pal.color(QPalette::ToolTipBase);
	const QColor tooltipText = pal.color(QPalette::ToolTipText);

	axisRect()->setBackground(baseColor);
	setBackground(windowColor);

	legend->setBorderPen(QPen(midColor));
	legend->setBrush(QBrush(windowColor));
	legend->setTextColor(textColor);

	xAxis->setBasePen(QPen(textColor));
	yAxis->setBasePen(QPen(textColor));
	xAxis->setTickPen(QPen(textColor));
	yAxis->setTickPen(QPen(textColor));
	xAxis->setSubTickPen(QPen(textColor));
	yAxis->setSubTickPen(QPen(textColor));
	xAxis->setTickLabelColor(textColor);
	yAxis->setTickLabelColor(textColor);
	xAxis->setLabelColor(textColor);
	yAxis->setLabelColor(textColor);

	xAxis->grid()->setPen(QPen(midColor, 1, Qt::DashLine));
	yAxis->grid()->setPen(QPen(midColor, 1, Qt::DashLine));
	xAxis->grid()->setSubGridPen(QPen(midLightColor, 1, Qt::DotLine));
	yAxis->grid()->setSubGridPen(QPen(midLightColor, 1, Qt::DotLine));

	m_tracer->setPen(QPen(highlightColor, 1));
	m_tracer->setBrush(QBrush(highlightColor));
	m_tracerLine->setPen(QPen(highlightColor, 1, Qt::DashLine));
	m_tracerLabel->setBrush(QBrush(tooltipBase));
	m_tracerLabel->setColor(tooltipText);
}

void UESTCQCustomPlot::refreshLegendButtonPosition()
{
	if (!m_btnLegendCollapse || !m_btnLegendExpand) {
		return;
	}
	const int margin = 8;
	const QSize size = m_btnLegendCollapse->sizeHint().expandedTo(QSize(72, 24));
	const QRect rect = axisRect()->rect();
	const int x = rect.left() + margin;
	const int y = rect.top() + margin;
	m_btnLegendCollapse->setGeometry(x, y, size.width(), size.height());
	m_btnLegendExpand->setGeometry(x, y, size.width(), size.height());
}

void UESTCQCustomPlot::toggleLegend(bool visible)
{
	legend->setVisible(visible);
	m_btnLegendCollapse->setVisible(visible);
	m_btnLegendExpand->setVisible(!visible);
	replot(QCustomPlot::rpQueuedReplot);
}

void UESTCQCustomPlot::setSampleIntervalMicroseconds(qint64 intervalUs)
{
	m_sampleIntervalUs = qMax<qint64>(1, intervalUs);
}

void UESTCQCustomPlot::setTimeAxisEnabled(bool enabled)
{
	m_timeAxisEnabled = enabled;
	if (enabled) {
		xAxis->setTicker(m_timeTicker);
	} else {
		xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new QCPAxisTicker()));
	}
}

void UESTCQCustomPlot::setAutoRangeEnabled(bool enabled)
{
	m_autoRangeEnabled = enabled;
}

void UESTCQCustomPlot::setAutoRangePadding(double ratio)
{
	m_autoRangePadding = qMax(0.0, ratio);
}

void UESTCQCustomPlot::setAutoRangeResumeMs(int ms)
{
	m_autoRangeResumeMs = qMax(0, ms);
}

void UESTCQCustomPlot::setDisplayWindowSeconds(double seconds)
{
	m_displayWindowSeconds = qMax(0.01, seconds);
}

bool UESTCQCustomPlot::isUserOverrideActive() const
{
	return m_userOverride;
}

double UESTCQCustomPlot::visibleWindowSeconds() const
{
	return xAxis ? xAxis->range().size() : 0.0;
}

QCPGraph *UESTCQCustomPlot::addRealTimeLine(const QString &name, const QColor &color)
{
	const int index = graphCount();
	QCPGraph *graph = addGraph();
	graph->setName(name);
	graph->setLineStyle(QCPGraph::lsLine);
	graph->setScatterStyle(QCPScatterStyle::ssNone);
	graph->setAdaptiveSampling(true);
	graph->setVisible(true);

	const QColor useColor = color.isValid() ? color : pickColor(index);
	graph->setPen(QPen(useColor, 2));
	graph->setSelectable(QCP::stNone);
	m_realTimeGraphs.insert(name, graph);

	replot(QCustomPlot::rpQueuedReplot);
	return graph;
}

void UESTCQCustomPlot::updateRealTimeLines(const QVector<QCPGraph *> &graphs,
										   const QVector<QVector<double>> &buffers)
{
	if (graphs.isEmpty() || graphs.size() != buffers.size()) {
		return;
	}

	const qint64 nowUs = static_cast<qint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000;
	m_lastTimestampUs = nowUs;
    const double nowSec = static_cast<double>(nowUs) / 1000000.0;

	for (int i = 0; i < graphs.size(); ++i) {
		QCPGraph *graph = graphs[i];
		if (!graph) {
			continue;
		}
		if (!graph->visible()) {
			continue;
		}
		const auto &buffer = buffers[i];
		if (buffer.isEmpty()) {
			continue;
		}

		const int count = buffer.size();
		QVector<double> keys(count);
		for (int j = 0; j < count; ++j) {
			const qint64 tsUs = nowUs - static_cast<qint64>(count - 1 - j) * m_sampleIntervalUs;
			keys[j] = static_cast<double>(tsUs) / 1000000.0;
		}
		graph->setData(keys, buffer);
	}

	if (m_autoRangeEnabled && !m_userOverride) {
		double minVal = 0.0;
		double maxVal = 0.0;
		bool hasRange = false;
		for (int i = 0; i < graphs.size(); ++i) {
			QCPGraph *graph = graphs[i];
			if (!graph || !graph->visible()) {
				continue;
			}
			const auto &buffer = buffers[i];
			if (buffer.isEmpty()) {
				continue;
			}
			for (double v : buffer) {
				if (!hasRange) {
					minVal = v;
					maxVal = v;
					hasRange = true;
				} else {
					minVal = qMin(minVal, v);
					maxVal = qMax(maxVal, v);
				}
			}
		}
		if (hasRange) {
			yAxis->setRange(minVal, maxVal);
			applyYAxisPadding();
		}
		if (m_timeAxisEnabled) {
			xAxis->setRange(nowSec - m_displayWindowSeconds, nowSec);
		}
	}
	replot(QCustomPlot::rpQueuedReplot);
}

void UESTCQCustomPlot::updateRealTimeLines(const QVector<QCPGraph *> &graphs,
                                           const QVector<QVector<double>> &buffers,
                                           const QVector<qint64> &intervalsUs)
{
	if (graphs.isEmpty() || graphs.size() != buffers.size() || graphs.size() != intervalsUs.size()) {
		return;
	}

	const qint64 nowUs = static_cast<qint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000;
	m_lastTimestampUs = nowUs;
	const double nowSec = static_cast<double>(nowUs) / 1000000.0;

	for (int i = 0; i < graphs.size(); ++i) {
		QCPGraph *graph = graphs[i];
		if (!graph || !graph->visible()) {
			continue;
		}
		const auto &buffer = buffers[i];
		if (buffer.isEmpty()) {
			continue;
		}

		const int count = buffer.size();
		const qint64 intervalUs = qMax<qint64>(1, intervalsUs[i]);
		QVector<double> keys(count);
		for (int j = 0; j < count; ++j) {
			const qint64 tsUs = nowUs - static_cast<qint64>(count - 1 - j) * intervalUs;
			keys[j] = static_cast<double>(tsUs) / 1000000.0;
		}
		graph->setData(keys, buffer);
	}

	if (m_autoRangeEnabled && !m_userOverride) {
		double minVal = 0.0;
		double maxVal = 0.0;
		bool hasRange = false;
		for (int i = 0; i < graphs.size(); ++i) {
			QCPGraph *graph = graphs[i];
			if (!graph || !graph->visible()) {
				continue;
			}
			const auto &buffer = buffers[i];
			if (buffer.isEmpty()) {
				continue;
			}
			for (double v : buffer) {
				if (!hasRange) {
					minVal = v;
					maxVal = v;
					hasRange = true;
				} else {
					minVal = qMin(minVal, v);
					maxVal = qMax(maxVal, v);
				}
			}
		}
		if (hasRange) {
			yAxis->setRange(minVal, maxVal);
			applyYAxisPadding();
		}
		if (m_timeAxisEnabled) {
			xAxis->setRange(nowSec - m_displayWindowSeconds, nowSec);
		}
	}
	if (m_timeAxisEnabled && m_userOverride) {
		xAxis->setRange(nowSec - m_displayWindowSeconds, nowSec);
	}
	replot(QCustomPlot::rpQueuedReplot);
}

void UESTCQCustomPlot::mousePressEvent(QMouseEvent *event)
{
	markUserInteraction();
	QCustomPlot::mousePressEvent(event);
}

void UESTCQCustomPlot::mouseReleaseEvent(QMouseEvent *event)
{
	markUserInteraction();
	QCustomPlot::mouseReleaseEvent(event);
}

void UESTCQCustomPlot::wheelEvent(QWheelEvent *event)
{
	markUserInteraction();
	QCustomPlot::wheelEvent(event);
}

void UESTCQCustomPlot::markUserInteraction()
{
	if (!m_autoRangeEnabled) {
		return;
	}
	m_userOverride = true;
	if (m_autoResumeTimer && m_autoRangeResumeMs > 0) {
		m_autoResumeTimer->start(m_autoRangeResumeMs);
	}
}

void UESTCQCustomPlot::resumeAutoRange()
{
	m_userOverride = false;
	if (m_autoRangeEnabled) {
		rescaleAxes(false);
		applyYAxisPadding();
		if (m_timeAxisEnabled) {
			const qint64 nowUs = static_cast<qint64>(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch()) * 1000;
			const double nowSec = static_cast<double>(nowUs) / 1000000.0;
			xAxis->setRange(nowSec - m_displayWindowSeconds, nowSec);
		}
		replot(QCustomPlot::rpQueuedReplot);
	}
}

void UESTCQCustomPlot::applyYAxisPadding()
{
	if (!yAxis) {
		return;
	}
	QCPRange range = yAxis->range();
	const double span = range.size();
	if (span <= 0.0) {
		range.lower -= 1.0;
		range.upper += 1.0;
	} else {
		const double pad = span * m_autoRangePadding;
		range.lower -= pad;
		range.upper += pad;
	}
	yAxis->setRange(range);
}

void UESTCQCustomPlot::downsampleStatic(const QVector<double> &x, const QVector<double> &y,
                                        QVector<double> *outX, QVector<double> *outY, int maxPoints) const
{
	if (!outX || !outY || maxPoints <= 0 || x.size() != y.size()) {
		return;
	}
	const int count = x.size();
	if (count <= maxPoints) {
		*outX = x;
		*outY = y;
		return;
	}
	outX->clear();
	outY->clear();
	outX->reserve(maxPoints);
	outY->reserve(maxPoints);
	const double step = static_cast<double>(count) / static_cast<double>(maxPoints);
	for (int i = 0; i < maxPoints; ++i) {
		const int idx = qBound(0, static_cast<int>(i * step), count - 1);
		outX->push_back(x[idx]);
		outY->push_back(y[idx]);
	}
}

QCPGraph *UESTCQCustomPlot::addStaticLine(const QString &name, const QVector<double> &x,
										  const QVector<double> &y, const QColor &color)
{
	if (x.size() != y.size() || x.isEmpty()) {
		return nullptr;
	}

	setTimeAxisEnabled(false);

	QVector<double> plotX;
	QVector<double> plotY;
	downsampleStatic(x, y, &plotX, &plotY, m_staticMaxPoints);

	const int index = graphCount();
	QCPGraph *graph = addGraph();
	graph->setName(name);
	graph->setLineStyle(QCPGraph::lsLine);
	graph->setScatterStyle(QCPScatterStyle::ssCircle);

	const QColor useColor = color.isValid() ? color : pickColor(index);
	graph->setPen(QPen(useColor, 2));
	graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, useColor, useColor.lighter(), 5));
	graph->setData(plotX, plotY);
	graph->setVisible(true);
	graph->setSelectable(QCP::stNone);
	m_staticGraphs.insert(name, graph);

	rescaleAxes(false);
	replot(QCustomPlot::rpQueuedReplot);
	return graph;
}

void UESTCQCustomPlot::updateStaticLine(QCPGraph *graph, const QVector<double> &x, const QVector<double> &y)
{
	if (!graph || x.size() != y.size()) {
		return;
	}
	QVector<double> plotX;
	QVector<double> plotY;
	downsampleStatic(x, y, &plotX, &plotY, m_staticMaxPoints);
	graph->setData(plotX, plotY);
	rescaleAxes(false);
	replot(QCustomPlot::rpQueuedReplot);
}

void UESTCQCustomPlot::removeStaticLine(const QString &name)
{
	if (!m_staticGraphs.contains(name)) {
		return;
	}
	QCPGraph *graph = m_staticGraphs.take(name);
	if (graph) {
		removeGraph(graph);
		replot(QCustomPlot::rpQueuedReplot);
	}
}

void UESTCQCustomPlot::handleLegendClick(QCPLegend *legendPtr, QCPAbstractLegendItem *item, QMouseEvent *event)
{
	Q_UNUSED(event)
	if (!legendPtr || !item) {
		return;
	}
	auto *legendItem = qobject_cast<QCPPlottableLegendItem *>(item);
	if (!legendItem) {
		return;
	}
	auto *graph = qobject_cast<QCPGraph *>(legendItem->plottable());
	if (!graph) {
		return;
	}
	graph->setVisible(!graph->visible());
	deselectAll();
	replot(QCustomPlot::rpQueuedReplot);
}

void UESTCQCustomPlot::refreshLegendItemAppearance(QCPGraph *graph)
{
	if (!graph) {
		return;
	}
	if (graph->visible()) {
		graph->setPen(QPen(graph->pen().color(), 2));
	} else {
		QColor muted = graph->pen().color();
		muted.setAlpha(80);
		graph->setPen(QPen(muted, 2, Qt::DashLine));
	}
}

void UESTCQCustomPlot::handleMouseMove(QMouseEvent *event)
{
	updateTracerAt(event->pos());
}

void UESTCQCustomPlot::handleMouseDoubleClick(QMouseEvent *event)
{
	Q_UNUSED(event)
	bool first = true;
	for (int i = 0; i < graphCount(); ++i) {
		QCPGraph *graph = this->graph(i);
		if (!graph || !graph->visible() || graph->data()->isEmpty()) {
			continue;
		}
		graph->rescaleAxes(first);
		first = false;
	}
	replot(QCustomPlot::rpQueuedReplot);
}

void UESTCQCustomPlot::updateTracerAt(const QPoint &pos)
{
	double key = 0.0;
	double value = 0.0;
	QCPGraph *graph = pickNearestGraph(pos, &key, &value);
	if (!graph) {
		m_tracer->setVisible(false);
		m_tracerLabel->setVisible(false);
		m_tracerLine->setVisible(false);
		replot(QCustomPlot::rpQueuedReplot);
		return;
	}

	m_tracer->setGraph(graph);
	m_tracer->setGraphKey(key);
	m_tracer->setInterpolating(true);
	m_tracer->updatePosition();

	QString xText;
	if (m_timeAxisEnabled) {
		const qint64 ms = static_cast<qint64>(key * 1000.0);
		const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms).toLocalTime();
		xText = dt.toString(QStringLiteral("HH:mm:ss.zzz"));
	} else {
		xText = QString::number(key, 'f', 6);
	}
	const QString labelText = QStringLiteral("%1\nX: %2\nY: %3")
		.arg(graph->name())
		.arg(xText)
		.arg(value, 0, 'f', 6);
	m_tracerLabel->setText(labelText);
	m_tracerLabel->position->setType(QCPItemPosition::ptAbsolute);
	const QPointF tracerPixel = m_tracer->position->pixelPosition();
	m_tracerLabel->position->setCoords(tracerPixel.x() + 10, tracerPixel.y() - 10);

	m_tracerLine->start->setType(QCPItemPosition::ptPlotCoords);
	m_tracerLine->end->setType(QCPItemPosition::ptPlotCoords);
	m_tracerLine->start->setCoords(key, yAxis->range().lower);
	m_tracerLine->end->setCoords(key, yAxis->range().upper);

	m_tracer->setVisible(true);
	m_tracerLabel->setVisible(true);
	m_tracerLine->setVisible(true);
	replot(QCustomPlot::rpQueuedReplot);
}

QCPGraph *UESTCQCustomPlot::pickNearestGraph(const QPoint &pos, double *outKey, double *outValue)
{
	const double x = xAxis->pixelToCoord(pos.x());
	QCPGraph *bestGraph = nullptr;
	double bestDist = std::numeric_limits<double>::max();

	for (int i = 0; i < graphCount(); ++i) {
		QCPGraph *graph = this->graph(i);
		if (!graph || !graph->visible()) {
			continue;
		}
		const auto data = graph->data();
		if (data->isEmpty()) {
			continue;
		}
		auto it = data->findBegin(x, true);
		if (it == data->constEnd()) {
			it = data->constEnd() - 1;
		}
		QCPGraphData point = *it;
		if (it != data->constBegin()) {
			auto prev = it - 1;
			const double prevDist = std::abs(prev->key - x);
			const double currDist = std::abs(point.key - x);
			if (prevDist < currDist) {
				point = *prev;
			}
		}

		const double px = xAxis->coordToPixel(point.key);
		const double py = yAxis->coordToPixel(point.value);
		const double dist = std::hypot(px - pos.x(), py - pos.y());
		if (dist < bestDist) {
			bestDist = dist;
			bestGraph = graph;
			if (outKey) {
				*outKey = point.key;
			}
			if (outValue) {
				*outValue = point.value;
			}
		}
	}

	if (bestDist > 80) {
		return nullptr;
	}
	return bestGraph;
}
