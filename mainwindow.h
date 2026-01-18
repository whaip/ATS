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
class LebalItemManager;
class DataCaptureCard;
class IRCamera;
class DataGenerateCard;
class FaultDiagnostic;
class BoardManager;
class JYThreadManager;
class JYDeviceWorker;

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
    LebalItemManager *m_maintainPage = nullptr;
    int m_maintainPageIndex = -1;
    DataCaptureCard *m_dataCapturePage = nullptr;
    int m_dataCapturePageIndex = -1;
    IRCamera *m_irCameraPage = nullptr;
    int m_irCameraPageIndex = -1;
    DataGenerateCard *m_dataGeneratePage = nullptr;
    int m_dataGeneratePageIndex = -1;
    FaultDiagnostic *m_faultDiagnosticPage = nullptr;
    int m_faultDiagnosticPageIndex = -1;
    BoardManager *m_boardManagerPage = nullptr;
    int m_boardManagerPageIndex = -1;
    bool m_irStationRunning = false;
    JYThreadManager *m_jyManager = nullptr;
    QMap<JYDeviceKind, JYDeviceWorker *> m_jyWorkers;
    QMap<JYDeviceKind, JYDeviceState> m_jyStates;
    JY8902Config m_capture8902Config;
};
#endif // MAINWINDOW_H
