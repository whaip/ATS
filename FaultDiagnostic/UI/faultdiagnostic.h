#ifndef FAULTDIAGNOSTIC_H
#define FAULTDIAGNOSTIC_H

#include <QImage>
#include <QHash>
#include <QMap>
#include <QVector>
#include <QWidget>
#include "../../ComponentsDetect/componenttypes.h"
#include "../Runtime/systemorchestration.h"
#include "../Core/testtaskcontextmanager.h"
#include "../Diagnostics/diagnosticdispatcher.h"
#include "../Diagnostics/diagnosticpluginmanager.h"
#include "../TPS/Manager/tpspluginmanager.h"

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

    struct TestTask {
        QString componentRef;
        QString pluginId;
        QMap<QString, QVariant> parameters;
    };

    struct ComponentViewData {
        QString id;
        QString taskId;
        QString name;
        QImage thermalImage;
        QVector<double> thermalMatrix;
        QSize thermalMatrixSize;
        QPointF thermalAlarmPoint;
        bool hasThermalAlarmPoint = false;
        QVector<double> x;
        QVector<double> y;
        QVector<double> x5322;
        QVector<double> y5322;
        QVector<double> x5323;
        QVector<double> y5323;
        QVector<double> x8902;
        QVector<double> y8902;
        QMap<QString, QVector<double>> signalXById;
        QMap<QString, QVector<double>> signalYById;
        QString reportHtml;
    };

    void setComponents(const QVector<ComponentViewData> &items);
    void updateComponent(const ComponentViewData &item);
    SystemRuntimeOrchestration *runtime() const;
    void setRuntimeThreadManager(class JYThreadManager *manager);
    void setRuntimeCameraStation(class CameraStation *station);
    void selectComponentById(const QString &id);
    void setGuidanceLabels(const QList<CompLabel> &labels);
    void setGuidanceImage(const QImage &image);
    void startBatchTest(const QVector<TestTask> &tasks);

public slots:
    void startTest();
    void startTestWith(const QString &componentRef,
                       const QString &pluginId,
                       const QMap<QString, QVariant> &parameters);

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void onComponentSelectionChanged();
    void onTestClicked();

private:
    void bindThreadManagerSignals();
    void buildWidgets();
    void applyThemeQss();
    void setCurrentIndex(int index);
    void runTest(const QString &componentRef,
                 const QString &pluginId,
                 const QMap<QString, QVariant> &parameters);
    void refreshThermal(const ComponentViewData &item);
    void refreshPlot(const ComponentViewData &item);
    void renderCurrentPlot(bool preserveView);
    void refreshReport(const ComponentViewData &item);

    Ui::FaultDiagnostic *ui;
    QListWidget *m_list = nullptr;
    IRCamera *m_tempView = nullptr;
    UESTCQCustomPlot *m_plot = nullptr;
    QTextBrowser *m_report = nullptr;
    QCPGraph *m_tempGraph = nullptr;
    QMap<QString, QCPGraph *> m_signalGraphs;

    QVector<ComponentViewData> m_components;
    QHash<QString, int> m_indexById;
    QList<CompLabel> m_guidanceAllLabels;
    QMap<QString, CompLabel> m_guidanceLabelsByRef;
    QImage m_guidanceImage;

    QString m_loadedTheme;
    bool m_applyingQss = false;
    SystemRuntimeOrchestration *m_runtime = nullptr;
    JYThreadManager *m_threadManager = nullptr;
    TPSPluginManager *m_tpsManager = nullptr;
    DiagnosticPluginManager *m_diagPluginManager = nullptr;
    DiagnosticDispatcher m_diagnosticDispatcher;
    TestTaskContextManager *m_taskContextManager = nullptr;
    bool m_devicesCreated = false;
    bool m_ownsThreadManager = false;
    bool m_plotViewportSyncing = false;
};

#endif // FAULTDIAGNOSTIC_H
