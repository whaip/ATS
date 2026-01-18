#include "mainwindow.h"
#include "logger.h"
#include "stylemanager.h"

#include "ComponentsDetect/yolomodel.h"
#include "ComponentsDetect/yolostation.h"

#include "HDCamera/camerastation.h"

#include <QApplication>

#include <thread>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    Logger::init();

    StyleManager::applyDefault(a);

    // Start YOLO inference station thread at app startup.
    YoloStation::instance()->start();

    // Start camera station thread at app startup.
    CameraStation::instance()->start();

    // Preload YOLO model in background so later entering ComponentsDetect is fast.
    std::thread([]() {
        try {
            (void)YOLOModel::getInstance();
            Logger::log(QStringLiteral("YOLO model preloaded"), Logger::Level::Info);
        } catch (...) {
            Logger::log(QStringLiteral("YOLO model preload failed"), Logger::Level::Error);
        }
    }).detach();

    MainWindow w;
    w.show();
    return a.exec();
}
