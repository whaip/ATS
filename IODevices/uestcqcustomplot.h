#ifndef UESTCQCUSTOMPLOT_H
#define UESTCQCUSTOMPLOT_H

#include <QtCore/QObject>
#include <QtCore/QHash>
#include <QtCore/QVector>

class QEvent;

#include "../include/qcustomplot.h"

class QCPAxisTicker;
class QToolButton;
class QTimer;

class UESTCQCustomPlot : public QCustomPlot
{
    Q_OBJECT
public:
    explicit UESTCQCustomPlot(QWidget *parent = nullptr);

    QCPGraph *addRealTimeLine(const QString &name, const QColor &color = QColor());
    void updateRealTimeLines(const QVector<QCPGraph *> &graphs, const QVector<QVector<double>> &buffers);
    void updateRealTimeLines(const QVector<QCPGraph *> &graphs,
                             const QVector<QVector<double>> &buffers,
                             const QVector<qint64> &intervalsUs);
    void updateRealTimeSeries(const QVector<QCPGraph *> &graphs,
                              const QVector<QVector<double>> &xBuffers,
                              const QVector<QVector<double>> &yBuffers,
                              bool autoFollow);

    QCPGraph *addStaticLine(const QString &name, const QVector<double> &x, const QVector<double> &y,
                            const QColor &color = QColor());
    void updateStaticLine(QCPGraph *graph, const QVector<double> &x, const QVector<double> &y, bool rescale = true);
    void removeStaticLine(const QString &name);

    void setSampleIntervalMicroseconds(qint64 intervalUs);
    void setTimeAxisEnabled(bool enabled);
    void setAutoRangeEnabled(bool enabled);
    void setAutoRangePadding(double ratio);
    void setAutoRangeResumeMs(int ms);
    void setDisplayWindowSeconds(double seconds);
    void resetAutoView();
    bool isUserOverrideActive() const;
    double visibleWindowSeconds() const;
    double visibleLowerBound() const;
    double visibleUpperBound() const;
    int plotAreaWidth() const;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setupPlot();
    void applyTheme();
    void refreshLegendButtonPosition();
    void toggleLegend(bool visible);
    void refreshLegendItemAppearance(QCPGraph *graph);
    void updateTracerAt(const QPoint &pos);
    QCPGraph *pickNearestGraph(const QPoint &pos, double *outKey, double *outValue);
    void markUserInteraction();
    void resumeAutoRange();
    bool computeVisibleYRange(double xLower, double xUpper, double *outMin, double *outMax) const;
    void applyVisibleAutoRange(bool resetX);
    void applyYAxisPadding();
    void downsampleStatic(const QVector<double> &x, const QVector<double> &y,
                          QVector<double> *outX, QVector<double> *outY, int maxPoints) const;

private slots:
    void handleLegendClick(QCPLegend *legend, QCPAbstractLegendItem *item, QMouseEvent *event);
    void handleMouseMove(QMouseEvent *event);
    void handleMouseDoubleClick(QMouseEvent *event);

private:
    QSharedPointer<QCPAxisTicker> m_timeTicker;
    QHash<QString, QCPGraph *> m_staticGraphs;
    QHash<QString, QCPGraph *> m_realTimeGraphs;
    QHash<QCPGraph *, QPen> m_graphBasePens;

    qint64 m_sampleIntervalUs = 50000;
    bool m_timeAxisEnabled = true;
    bool m_autoRangeEnabled = true;
    bool m_userOverride = false;
    double m_autoRangePadding = 0.08;
    int m_autoRangeResumeMs = 30000;
    QTimer *m_autoResumeTimer = nullptr;
    double m_displayWindowSeconds = 1.0;
    int m_staticMaxPoints = 1000000;

    QCPItemTracer *m_tracer = nullptr;
    QCPItemText *m_tracerLabel = nullptr;
    QCPItemLine *m_tracerLine = nullptr;

    QToolButton *m_btnLegendCollapse = nullptr;
    QToolButton *m_btnLegendExpand = nullptr;
};

#endif // UESTCQCUSTOMPLOT_H
