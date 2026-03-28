QT       += core gui concurrent printsupport openglwidgets serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    BoardManager/boardmanager.cpp \
    ComponentsDetect/componentsdetect.cpp \
    ComponentsDetect/yolomodel.cpp \
    ComponentsDetect/yolostation.cpp \
    ComponentsDetect/yolostationclient.cpp \
    FaultDiagnostic/UI/configurationwindow.cpp \
    FaultDiagnostic/UI/faultdiagnostic.cpp \
    FaultDiagnostic/Workflow/BatchParameterReview/batchparamreviewdialog.cpp \
    FaultDiagnostic/Workflow/PortAllocation/portallocationreviewdialog.cpp \
    FaultDiagnostic/Workflow/TemperatureRoi/temperatureroiselectdialog.cpp \
    FaultDiagnostic/Workflow/WiringGuide/wiringguidedialog.cpp \
    FaultDiagnostic/Core/testsequencemanager.cpp \
    FaultDiagnostic/Core/testtaskcontextmanager.cpp \
    FaultDiagnostic/Core/deviceportplanner.cpp \
    FaultDiagnostic/Core/deviceportmanager.cpp \
    FaultDiagnostic/Core/deviceportmanager/deviceportmanager/deviceportmanagerwidget.cpp \
    FaultDiagnostic/Core/captureddatamanager.cpp \
    FaultDiagnostic/Diagnostics/diagnosticdispatcher.cpp \
    FaultDiagnostic/Diagnostics/diagnosticdatamapper.cpp \
    FaultDiagnostic/Diagnostics/diagnosticpluginmanager.cpp \
    FaultDiagnostic/Diagnostics/Plugins/capacitordiagnosticplugin.cpp \
    FaultDiagnostic/Diagnostics/Plugins/inductordiagnosticplugin.cpp \
    FaultDiagnostic/Diagnostics/Plugins/multitpsdiagnosticplugin.cpp \
    FaultDiagnostic/Diagnostics/Plugins/resistordiagnosticplugin.cpp \
    FaultDiagnostic/Diagnostics/Plugins/typicaldiagnosticplugin.cpp \
    FaultDiagnostic/Diagnostics/Plugins/transistordiagnosticplugin.cpp \
    FaultDiagnostic/Runtime/systemorchestration.cpp \
    FaultDiagnostic/TPS/Manager/tpspluginmanager.cpp \
    FaultDiagnostic/TPS/Manager/tpsbuiltinregistry.cpp \
    FaultDiagnostic/TPS/Core/tpsruntimecontext.cpp \
    FaultDiagnostic/TPS/Plugins/exampletpsplugin.cpp \
    FaultDiagnostic/TPS/Plugins/resistancetpsplugin.cpp \
    FaultDiagnostic/TPS/Plugins/capacitortpsplugin.cpp \
    FaultDiagnostic/TPS/Plugins/inductortpsplugin.cpp \
    FaultDiagnostic/TPS/Plugins/transistortpsplugin.cpp \
    FaultDiagnostic/TPS/Plugins/multitpsplugin.cpp \
    FaultDiagnostic/TPS/Plugins/typicaltpsplugin.cpp \
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
    IODevices/JYDevices/jydeviceconfigutils.cpp \
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
    boardrepository.cpp \
    componenttyperegistry.cpp \
    tpsparamservice.cpp \
    tool/labelrectitem.cpp \
    tool/labelediting.cpp \
    tool/lebalitemmanager.cpp \
    tool/ch340.cpp \
    tool/pcb_extract.cpp \
    tool/pcbextract.cpp \
    tool/siftmatcher.cpp

HEADERS += \
    BoardManager/boardmanager.h \
    ComponentsDetect/componentsdetect.h \
    ComponentsDetect/componenttypes.h \
    ComponentsDetect/yolomodel.h \
    ComponentsDetect/yolostation.h \
    ComponentsDetect/yolostationclient.h \
    FaultDiagnostic/UI/configurationwindow.h \
    FaultDiagnostic/UI/faultdiagnostic.h \
    FaultDiagnostic/Workflow/BatchParameterReview/batchparamreviewdialog.h \
    FaultDiagnostic/Workflow/PortAllocation/portallocationreviewdialog.h \
    FaultDiagnostic/Workflow/TemperatureRoi/temperatureroiselectdialog.h \
    FaultDiagnostic/Workflow/WiringGuide/wiringguidedialog.h \
    FaultDiagnostic/Core/testsequencemanager.h \
    FaultDiagnostic/Core/testtaskcontextmanager.h \
    FaultDiagnostic/Core/deviceportplanner.h \
    FaultDiagnostic/Core/deviceportmanager.h \
    FaultDiagnostic/Core/deviceportmanager/deviceportmanager/deviceportmanagerwidget.h \
    FaultDiagnostic/Core/captureddatamanager.h \
    FaultDiagnostic/Diagnostics/diagnosticdatatypes.h \
    FaultDiagnostic/Diagnostics/diagnosticalgorithm.h \
    FaultDiagnostic/Diagnostics/diagnosticdispatcher.h \
    FaultDiagnostic/Diagnostics/diagnosticdatamapper.h \
    FaultDiagnostic/Diagnostics/diagnosticplugininterface.h \
    FaultDiagnostic/Diagnostics/diagnosticpluginmanager.h \
    FaultDiagnostic/Diagnostics/Plugins/capacitordiagnosticplugin.h \
    FaultDiagnostic/Diagnostics/Plugins/inductordiagnosticplugin.h \
    FaultDiagnostic/Diagnostics/Plugins/multitpsdiagnosticplugin.h \
    FaultDiagnostic/Diagnostics/Plugins/resistordiagnosticplugin.h \
    FaultDiagnostic/Diagnostics/Plugins/typicaldiagnosticplugin.h \
    FaultDiagnostic/Diagnostics/Plugins/transistordiagnosticplugin.h \
    FaultDiagnostic/Runtime/systemorchestration.h \
    FaultDiagnostic/TPS/Manager/tpspluginmanager.h \
    FaultDiagnostic/TPS/Manager/tpsbuiltinregistry.h \
    FaultDiagnostic/TPS/Core/tpsplugininterface.h \
    FaultDiagnostic/TPS/Core/tpsmodels.h \
    FaultDiagnostic/TPS/Core/tpsruntimecontext.h \
    FaultDiagnostic/TPS/Plugins/exampletpsplugin.h \
    FaultDiagnostic/TPS/Plugins/resistancetpsplugin.h \
    FaultDiagnostic/TPS/Plugins/capacitortpsplugin.h \
    FaultDiagnostic/TPS/Plugins/inductortpsplugin.h \
    FaultDiagnostic/TPS/Plugins/transistortpsplugin.h \
    FaultDiagnostic/TPS/Plugins/multitpsplugin.h \
    FaultDiagnostic/TPS/Plugins/typicaltpsplugin.h \
    FaultDiagnostic/Core/testplan.h \
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
    IODevices/JYDevices/jydeviceconfigutils.h \
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
    boardrepository.h \
    componenttyperegistry.h \
    tpsparamservice.h \
    tool/labelrectitem.h \
    tool/labelediting.h \
    tool/lebalitemmanager.h \
    tool/ch340.h \
    tool/pcb_extract.h \
    tool/pcbextract.h \
    tool/siftmatcher.h

FORMS += \
    BoardManager/boardmanager.ui \
    ComponentsDetect/componentsdetect.ui \
    FaultDiagnostic/UI/configurationwindow.ui \
    FaultDiagnostic/UI/faultdiagnostic.ui \
    FaultDiagnostic/Workflow/PortAllocation/portallocationreviewdialog.ui \
    FaultDiagnostic/Workflow/WiringGuide/wiringguidedialog.ui \
    FaultDiagnostic/Core/deviceportmanager/deviceportmanager/deviceportmanager.ui \
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
    RC_ICONS = $$PWD/build/release/app.ico

    CONFIG(debug, debug|release) {
        DESTDIR = $$PWD/build/debug
    } else {
        DESTDIR = $$PWD/build/release
    }

    # 复制必要的DLL文件
    QMAKE_POST_LINK += $$quote(xcopy /Y /Q $$shell_path($$PWD/bin/*.dll) $$shell_path($$DESTDIR) > nul$$escape_expand(\n\t))
    QMAKE_POST_LINK += $$quote(copy /Y $$shell_path($$PWD/build/release/app.ico) $$shell_path($$DESTDIR\\app.ico) > nul$$escape_expand(\n\t))
    QMAKE_POST_LINK += $$quote(copy /Y $$shell_path($$PWD/build/release/ATS.png) $$shell_path($$DESTDIR\\ATS.png) > nul$$escape_expand(\n\t))
    QMAKE_POST_LINK += $$quote(if not exist $$shell_path($$DESTDIR\\model) mkdir $$shell_path($$DESTDIR\\model)$$escape_expand(\n\t))
    QMAKE_POST_LINK += $$quote(xcopy /Y /Q $$shell_path($$PWD/ComponentsDetect/model/*) $$shell_path($$DESTDIR\\model\\) > nul$$escape_expand(\n\t))

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
