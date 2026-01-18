#ifndef PCBEXTRACT_H
#define PCBEXTRACT_H

#include <QImage>
#include <QRectF>
#include <QWidget>

namespace Ui {
class PCBExtractWidget;
}

class ZoomableGraphicsView;
class QGraphicsScene;
class QGraphicsPathItem;
class QGraphicsRectItem;

struct PcbExtractResult {
    QImage source;
    QImage cropped;
    QRectF rect;
};

class PCBExtractWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PCBExtractWidget(QWidget *parent = nullptr);
    ~PCBExtractWidget();

    void setInputImage(const QImage &image);
    PcbExtractResult output() const;

signals:
    void confirmed(const QImage &source, const QImage &cropped, const QRectF &rect);

private slots:
    void onRedetect();
    void onConfirm();

private:
    void ensureSceneItems();
    void updateOverlay();
    void applyDetectedRect(const QRectF &rect);
    QRectF currentRectInScene() const;
    QRectF clampToImage(const QRectF &rect) const;
    void updateOutput();

    Ui::PCBExtractWidget *ui;
    ZoomableGraphicsView *m_view = nullptr;
    QGraphicsScene *m_scene = nullptr;
    QGraphicsPathItem *m_overlayItem = nullptr;
    QGraphicsRectItem *m_cropItem = nullptr;
    QImage m_inputImage;
    PcbExtractResult m_output;
};

#endif // PCBEXTRACT_H
