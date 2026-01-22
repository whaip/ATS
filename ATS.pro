QT       += core gui concurrent printsupport openglwidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    BoardManager/boardmanager.cpp \
    BoardManager/boarddatamanager.cpp \
    ComponentsDetect/componentsdetect.cpp \
    ComponentsDetect/yolomodel.cpp \
    ComponentsDetect/yolostation.cpp \
    ComponentsDetect/yolostationclient.cpp \
    FaultDiagnostic/configurationwindow.cpp \
    FaultDiagnostic/faultdiagnostic.cpp \
    HDCamera/hdcamera.cpp \
    HDCamera/camerastation.cpp \
    HDCamera/camerastationclient.cpp \
    HDCamera/dshowcamerautil.cpp \
    HDCamera/zoomablegraphicsview.cpp \
    IODevices/DataCaptureCard/datacapturecard.cpp \
    IODevices/DataGenerateCard/datageneratecard.cpp \
    IODevices/JYDevices/jydeviceadapter.cpp \
    IODevices/JYDevices/jydeviceorchestrator.cpp \
    IODevices/JYDevices/jydeviceworker.cpp \
    IODevices/JYDevices/jydataaligner.cpp \
    IODevices/JYDevices/jydatapipeline.cpp \
    IODevices/JYDevices/jythreadmanager.cpp \
    IODevices/uestcqcustomplot.cpp \
    IRCamera/ircamera.cpp \
    IRCamera/ircamerastation.cpp \
    IRCamera/ircamerastationclient.cpp \
    include/qcustomplot.cpp \
    logdispatcher.cpp \
    logger.cpp \
    main.cpp \
    mainwindow.cpp \
    pagebuttonmanager.cpp \
    stylemanager.cpp \
    tool/labelrectitem.cpp \
    tool/labelediting.cpp \
    tool/lebalitemmanager.cpp \
    tool/pcb_extract.cpp \
    tool/pcbextract.cpp \
    tool/siftmatcher.cpp

HEADERS += \
    BoardManager/boardmanager.h \
    BoardManager/boarddatamanager.h \
    ComponentsDetect/componentsdetect.h \
    ComponentsDetect/componenttypes.h \
    ComponentsDetect/yolomodel.h \
    ComponentsDetect/yolostation.h \
    ComponentsDetect/yolostationclient.h \
    FaultDiagnostic/configurationwindow.h \
    FaultDiagnostic/faultdiagnostic.h \
    FaultDiagnostic/testplan.h \
    HDCamera/hdcamera.h \
    HDCamera/camerastation.h \
    HDCamera/camerastationclient.h \
    HDCamera/cameratypes.h \
    HDCamera/dshowcamerautil.h \
    HDCamera/zoomablegraphicsview.h \
    IODevices/DataCaptureCard/datacapturecard.h \
    IODevices/DataGenerateCard/datageneratecard.h \
    IODevices/JYDevices/jydatachannel.h \
    IODevices/JYDevices/jydataaligner.h \
    IODevices/JYDevices/jydatapipeline.h \
    IODevices/JYDevices/jydeviceadapter.h \
    IODevices/JYDevices/jydeviceorchestrator.h \
    IODevices/JYDevices/jydeviceworker.h \
    IODevices/JYDevices/5711waveformconfig.h \
    IODevices/JYDevices/jydevicetype.h \
    IODevices/JYDevices/jythreadmanager.h \
    IODevices/portdefinitions.h \
    IODevices/uestcqcustomplot.h \
    IRCamera/ircamera.h \
    IRCamera/ircamerastation.h \
    IRCamera/ircamerastationclient.h \
    include/qcustomplot.h \
    logdispatcher.h \
    logger.h \
    mainwindow.h \
    pagebuttonmanager.h \
    stylemanager.h \
    tool/labelrectitem.h \
    tool/labelediting.h \
    tool/lebalitemmanager.h \
    tool/pcb_extract.h \
    tool/pcbextract.h \
    tool/siftmatcher.h

FORMS += \
    BoardManager/boardmanager.ui \
    ComponentsDetect/componentsdetect.ui \
    FaultDiagnostic/configurationwindow.ui \
    FaultDiagnostic/faultdiagnostic.ui \
    HDCamera/hdcamera.ui \
    IODevices/DataCaptureCard/datacapturecard.ui \
    IODevices/DataGenerateCard/datageneratecard.ui \
    IRCamera/ircamera.ui \
    mainwindow.ui \
    tool/lebalitemmanager.ui \
    tool/pcbextract.ui

RESOURCES += \
    resources.qrc

INCLUDEPATH += include \
               include/onnxruntime \
               include/opencv2

# 库路径
win32 {
    CONFIG(debug, debug|release) {
        DESTDIR = $$PWD/build/debug
    } else {
        DESTDIR = $$PWD/build/release
    }

    # 复制必要的DLL文件
    QMAKE_POST_LINK += $$quote(xcopy /Y /Q $$shell_path($$PWD/bin/*.dll) $$shell_path($$DESTDIR) > nul$$escape_expand(\n\t))

    LIBS += -L$$PWD/lib/windows -L$$PWD/lib/pxie5320 -L$$PWD/lib/pxie5711 -L$$PWD/lib/pxie8902
    LIBS += -L$$PWD/lib/fftw -L$$PWD/lib/opencv_release -L$$PWD/lib/onnxruntime
    
    # RtNet红外摄像头库
    contains(QT_ARCH, x86_64) {
        LIBS += -L$$PWD/lib/windows/x64 -lRtNet
    } else {
        LIBS += -L$$PWD/lib/windows/x86 -lRtNet
    }
    
    # JYTEK设备库
    LIBS += -lJY5320Core -lJY5710Core -lJY8902Core
      # FFTW库
    LIBS += -lfftw3
    
    # OpenCV库
    LIBS += -lopencv_world4110
    
    # ONNX Runtime库
    LIBS += -lonnxruntime

    # DirectShow (device capability enumeration)
    LIBS += -lstrmiids -lole32 -loleaut32 -luuid
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
