#ifndef FAULTDIAGNOSTIC_H
#define FAULTDIAGNOSTIC_H

#include <QImage>
#include <QHash>
#include <QVector>
#include <QWidget>

class IRCamera;
class QTextBrowser;
class QListWidget;
class UESTCQCustomPlot;
class QCPGraph;
class QEvent;

namespace Ui {
class FaultDiagnostic;
}

class FaultDiagnostic : public QWidget
{
    Q_OBJECT

public:
    explicit FaultDiagnostic(QWidget *parent = nullptr);
    ~FaultDiagnostic();

    struct ComponentViewData {
        QString id;
        QString name;
        QImage thermalImage;
        QVector<double> thermalMatrix;
        QSize thermalMatrixSize;
        QVector<double> x;
        QVector<double> y;
        QString reportHtml;
    };

    void setComponents(const QVector<ComponentViewData> &items);
    void updateComponent(const ComponentViewData &item);

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void onComponentSelectionChanged();

private:
    void buildWidgets();
    void applyThemeQss();
    void setCurrentIndex(int index);
    void refreshThermal(const ComponentViewData &item);
    void refreshPlot(const ComponentViewData &item);
    void refreshReport(const ComponentViewData &item);

    Ui::FaultDiagnostic *ui;
    QListWidget *m_list = nullptr;
    IRCamera *m_tempView = nullptr;
    UESTCQCustomPlot *m_plot = nullptr;
    QTextBrowser *m_report = nullptr;
    QCPGraph *m_staticGraph = nullptr;

    QVector<ComponentViewData> m_components;
    QHash<QString, int> m_indexById;

    QString m_loadedTheme;
    bool m_applyingQss = false;
};

#endif // FAULTDIAGNOSTIC_H
