#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QApplication>
#include "stylemanager.h"
#include <QAction>
#include <QMenu>
#include <QString>
#include <QToolBar>
#include <QToolButton>
#include <QMap>

#include "IODevices/JYDevices/jydevicetype.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class PageButtonManager;
class ComponentsDetect;
class DataCaptureCard;
class IRCamera;
class DataGenerateCard;
class FaultDiagnostic;
class BoardManager;
class ConfigurationWindow;
class JYThreadManager;
class JYDeviceWorker;
class TaskLogStatisticsPage;
class RuntimeRecordsPage;
class QTextBrowser;
class QTableWidget;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public slots:
    void on_LightTheme_triggered() { if (qApp) {
            StyleManager::applyTheme(*qApp, StyleManager::Theme::Light);
        }};
    void on_DarkTheme_triggered(){ if (qApp) {
            StyleManager::applyTheme(*qApp, StyleManager::Theme::Dark);
        }};

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    int addPage(QWidget *page, const QString &pageName = QString(), bool switchToPage = true);
    int addEmptyPage(const QString &pageName, bool switchToPage = true);

private:
    Ui::MainWindow *ui;
    PageButtonManager *m_pageButtonManager = nullptr;
    ComponentsDetect *m_componentsDetectPage = nullptr;
    int m_componentsDetectPageIndex = -1;
    DataCaptureCard *m_dataCapturePage = nullptr;
    int m_dataCapturePageIndex = -1;
    IRCamera *m_irCameraPage = nullptr;
    int m_irCameraPageIndex = -1;
    DataGenerateCard *m_dataGeneratePage = nullptr;
    int m_dataGeneratePageIndex = -1;
    FaultDiagnostic *m_faultDiagnosticPage = nullptr;
    int m_faultDiagnosticPageIndex = -1;
    ConfigurationWindow *m_configurationPage = nullptr;
    int m_configurationPageIndex = -1;
    BoardManager *m_boardManagerPage = nullptr;
    int m_boardManagerPageIndex = -1;
    QWidget *m_errorLogPage = nullptr;
    int m_errorLogPageIndex = -1;
    QTableWidget *m_errorLogPageTable = nullptr;
    QTextBrowser *m_errorLogDetailBrowser = nullptr;
    QWidget *m_projectInfoPage = nullptr;
    int m_projectInfoPageIndex = -1;
    QTextBrowser *m_projectInfoBrowser = nullptr;
    TaskLogStatisticsPage *m_taskLogStatisticsPage = nullptr;
    int m_taskLogStatisticsPageIndex = -1;
    RuntimeRecordsPage *m_runtimeRecordsPage = nullptr;
    int m_runtimeRecordsPageIndex = -1;
    bool m_irStationRunning = false;
    JYThreadManager *m_jyManager = nullptr;
    QMap<JYDeviceKind, JYDeviceWorker *> m_jyWorkers;
    QMap<JYDeviceKind, JYDeviceState> m_jyStates;
    JY8902Config m_capture8902Config;
};
#endif // MAINWINDOW_H
