#ifndef FAULTDIAGNOSTIC_H
#define FAULTDIAGNOSTIC_H

#include <QImage>
#include <QHash>
#include <QVector>
#include <QWidget>
#include "../Runtime/systemorchestration.h"
#include "../TPS/Manager/tpspluginmanager.h"
#include "../TPS/Plugins/resistancetpsplugin.h"
#include "../TPS/Plugins/multitpsplugin.h"

class JYThreadManager;

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
        QVector<double> x5322;
        QVector<double> y5322;
        QVector<double> x5323;
        QVector<double> y5323;
        QString reportHtml;
    };

    void setComponents(const QVector<ComponentViewData> &items);
    void updateComponent(const ComponentViewData &item);
    SystemRuntimeOrchestration *runtime() const;
    void setRuntimeThreadManager(class JYThreadManager *manager);
    void setRuntimeCameraStation(class CameraStation *station);

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void onComponentSelectionChanged();
    void onTestClicked();

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
    QCPGraph *m_tempGraph = nullptr;
    QCPGraph *m_graph5322 = nullptr;
    QCPGraph *m_graph5323 = nullptr;

    QVector<ComponentViewData> m_components;
    QHash<QString, int> m_indexById;

    QString m_loadedTheme;
    bool m_applyingQss = false;
    SystemRuntimeOrchestration *m_runtime = nullptr;
    JYThreadManager *m_threadManager = nullptr;
    TPSPluginManager *m_tpsManager = nullptr;
    ResistanceTpsPlugin *m_resistancePlugin = nullptr;
    MultiSignalTpsPlugin *m_multiPlugin = nullptr;
    bool m_devicesCreated = false;
};

#endif // FAULTDIAGNOSTIC_H
