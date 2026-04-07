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
class TestTaskLogService;

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

    // 批量测试入口中的一条任务描述。
    struct TestTask {
        QString componentRef;
        QString pluginId;
        QMap<QString, QVariant> parameters;
    };

    // 单个元件在界面上的展示数据，包括热像、波形和报告。
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

    // 设置待显示元件列表；运行过程中也可单独增量更新。
    void setComponents(const QVector<ComponentViewData> &items);
    void updateComponent(const ComponentViewData &item);
    SystemRuntimeOrchestration *runtime() const;
    // 注入设备线程管理器和相机站，供测试流程复用。
    void setRuntimeThreadManager(class JYThreadManager *manager);
    void setRuntimeCameraStation(class CameraStation *station);
    void selectComponentById(const QString &id);
    void setGuidanceLabels(const QList<CompLabel> &labels);
    void setGuidanceImage(const QImage &image);
    void setCurrentBoardId(const QString &boardId);
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
    // 绑定设备线程、绘图和界面控件的信号。
    void bindThreadManagerSignals();
    void buildWidgets();
    void applyThemeQss();
    void setCurrentIndex(int index);
    // 执行单个元件的完整测试链路：TPS、采集、诊断、报告、日志。
    void runTest(const QString &componentRef,
                 const QString &pluginId,
                 const QMap<QString, QVariant> &parameters);
    void refreshThermal(const ComponentViewData &item);
    void refreshPlot(const ComponentViewData &item);
    void renderCurrentPlot(bool preserveView);
    void refreshReport(const ComponentViewData &item);
    // 将任务上下文落库到统计日志模块。
    void appendTaskLog(const QString &taskId);

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
    TestTaskLogService *m_taskLogService = nullptr;
    bool m_devicesCreated = false;
    bool m_ownsThreadManager = false;
    bool m_plotViewportSyncing = false;
    QString m_currentBoardId;
};

#endif // FAULTDIAGNOSTIC_H
