#ifndef TEMPERATUREROISELECTDIALOG_H
#define TEMPERATUREROISELECTDIALOG_H

#include <QDialog>
#include <QRectF>
#include <QVector>

class IRCamera;

class TemperatureRoiSelectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TemperatureRoiSelectDialog(QWidget *parent = nullptr);

    QRectF selectedRoi() const;
    QVector<QRectF> selectedRois() const;
    bool hasSelectedRoi() const;

private slots:
    void onAcceptClicked();

private:
    IRCamera *m_camera = nullptr;
    QRectF m_selectedRoi;
    QVector<QRectF> m_selectedRois;
    bool m_hasSelectedRoi = false;
};

#endif // TEMPERATUREROISELECTDIALOG_H
