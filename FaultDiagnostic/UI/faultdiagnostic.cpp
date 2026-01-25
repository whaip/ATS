#include "faultdiagnostic.h"
#include "ui_faultdiagnostic.h"

#include "../../IRCamera/ircamera.h"
#include "../../IODevices/uestcqcustomplot.h"
#include "../../HDCamera/camerastation.h"
#include "../../IODevices/JYDevices/jythreadmanager.h"
#include "../../IRCamera/ircamerastation.h"
#include "../Core/deviceportplanner.h"
#include "../Core/deviceportmanager.h"
#include <QMessageBox>

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QListWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QSplitter>
#include <QTimer>

FaultDiagnostic::FaultDiagnostic(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FaultDiagnostic)
{
    ui->setupUi(this);
    buildWidgets();
    applyThemeQss();
    m_runtime = new SystemRuntimeOrchestration(this);
    m_runtime->setCameraStation(CameraStation::instance());
    m_threadManager = new JYThreadManager(this);
    m_runtime->setThreadManager(m_threadManager);
    if (m_runtime->frm()) {
        FrameworkSecurityService::UserContext user;
        user.userId = QStringLiteral("local");
        user.role = QStringLiteral("admin");
        m_runtime->frm()->setUser(user);
    }

    m_tpsManager = new TPSPluginManager(this);
    m_resistancePlugin = new ResistanceTpsPlugin(this);
    m_multiPlugin = new MultiSignalTpsPlugin(this);
    m_tpsManager->addBuiltin(m_resistancePlugin);
    m_tpsManager->addBuiltin(m_multiPlugin);
    m_tpsManager->loadAll(nullptr);

    if (m_list) {
        connect(m_list, &QListWidget::currentRowChanged, this, &FaultDiagnostic::onComponentSelectionChanged);
    }
    if (ui->pbTest) {
        connect(ui->pbTest, &QPushButton::clicked, this, &FaultDiagnostic::onTestClicked);
    }
}

FaultDiagnostic::~FaultDiagnostic()
{
    delete ui;
}

void FaultDiagnostic::setComponents(const QVector<ComponentViewData> &items)
{
    m_components = items;
    m_indexById.clear();

    if (!m_list) {
        return;
    }

    m_list->clear();
    for (int i = 0; i < m_components.size(); ++i) {
        const auto &item = m_components[i];
        const QString label = item.name.isEmpty() ? item.id : item.name;
        m_list->addItem(label.isEmpty() ? QStringLiteral("未命名器件") : label);
        if (!item.id.isEmpty()) {
            m_indexById.insert(item.id, i);
        }
    }

    if (!m_components.isEmpty()) {
        m_list->setCurrentRow(0);
        setCurrentIndex(0);
    } else {
        refreshThermal(ComponentViewData{});
        refreshPlot(ComponentViewData{});
        refreshReport(ComponentViewData{});
    }
}

void FaultDiagnostic::updateComponent(const ComponentViewData &item)
{
    if (!m_list) {
        return;
    }

    const int existing = item.id.isEmpty() ? -1 : m_indexById.value(item.id, -1);
    if (existing >= 0 && existing < m_components.size()) {
        m_components[existing] = item;
        const QString label = item.name.isEmpty() ? item.id : item.name;
        if (auto *w = m_list->item(existing)) {
            w->setText(label.isEmpty() ? QStringLiteral("未命名器件") : label);
        }
        if (m_list->currentRow() == existing) {
            setCurrentIndex(existing);
        }
        return;
    }

    m_components.push_back(item);
    const int index = m_components.size() - 1;
    const QString label = item.name.isEmpty() ? item.id : item.name;
    m_list->addItem(label.isEmpty() ? QStringLiteral("未命名器件") : label);
    if (!item.id.isEmpty()) {
        m_indexById.insert(item.id, index);
    }
    if (m_list->currentRow() < 0) {
        m_list->setCurrentRow(index);
        setCurrentIndex(index);
    }
}

void FaultDiagnostic::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event) {
        return;
    }
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        applyThemeQss();
    }
}

void FaultDiagnostic::onComponentSelectionChanged()
{
    if (!m_list) {
        return;
    }
    setCurrentIndex(m_list->currentRow());
}

void FaultDiagnostic::onTestClicked()
{
    const QString pluginId = QStringLiteral("tps.multi.signal");
    TPSPluginInterface *plugin = m_tpsManager ? m_tpsManager->plugin(pluginId) : nullptr;
    if (!plugin) {
        QMessageBox::warning(this, tr("测试"), tr("未找到电阻测试插件。"));
        return;
    }

    const DevicePortPlanner::Request planRequest = DevicePortPlanner::buildDefaultRequest();
    const DevicePortManager::Allocation allocation = DevicePortManager::allocate(planRequest);

    if (m_threadManager && !m_devicesCreated) {
        if (!planRequest.outputs5711.isEmpty()) {
            m_threadManager->create5711Worker();
        }
        if (planRequest.capture5322Ports > 0) {
            m_threadManager->create532xWorker(JYDeviceKind::PXIe5322);
        }
        if (planRequest.capture5323Ports > 0) {
            m_threadManager->create532xWorker(JYDeviceKind::PXIe5323);
        }
        if (planRequest.needResistance) {
            m_threadManager->create8902Worker();
        }
        m_devicesCreated = true;
    }

    TPSRequest request;
    request.runId = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    request.boardId = QStringLiteral("board");
    request.createdAt = QDateTime::currentDateTime();

    UTRItem item;
    const int currentIndex = m_list ? m_list->currentRow() : -1;
    if (currentIndex >= 0 && currentIndex < m_components.size()) {
        item.componentRef = m_components[currentIndex].id;
    }
    if (item.componentRef.isEmpty()) {
        item.componentRef = QStringLiteral("R1");
    }
    item.planId = QStringLiteral("resistance.basic");
    item.parameters.insert(QStringLiteral("nominalOhms"), 1000.0);
    item.parameters.insert(QStringLiteral("tolerancePercent"), 5.0);
    request.items.push_back(item);

    QMap<QString, QVariant> settings;
    settings.insert(QStringLiteral("nominalOhms"), item.parameters.value(QStringLiteral("nominalOhms")));
    settings.insert(QStringLiteral("tolerancePercent"), item.parameters.value(QStringLiteral("tolerancePercent")));
    QString error;
    plugin->configure(settings, &error);

    if (m_runtime && m_runtime->rms()) {
        m_runtime->rms()->setMappings(allocation.mappings);
    }

    bool started = true;
    if (m_runtime && m_runtime->rts()) {
        QString runError;
        const QVector<SignalRequest> requests = allocation.requests;
        started = m_runtime->rts()->startRun(request.runId,
                       request.boardId,
                       requests,
                       allocation.cfg532x,
                       allocation.cfg5711,
                       allocation.cfg8902,
                       2000,
                       &runError);
        if (!runError.isEmpty()) {
            QMessageBox::information(this, tr("测试"), tr("运行时启动失败：%1").arg(runError));
        }
    }

    if (!started) {
        return;
    }

    struct TempCapture {
        IRCameraStation::Frame frame;
        IRCameraStation::PointResult point;
        IRCameraStation::BoxResult box;
        bool hasFrame = false;
        bool hasPoint = false;
        bool hasBox = false;
        qint64 t0ms = -1;
        QVector<double> tempTimes;
        QVector<double> tempValues;
        QVector<double> x5322;
        QVector<double> y5322;
        QVector<double> x5323;
        QVector<double> y5323;
    };
    auto capture = QSharedPointer<TempCapture>::create();
    IRCameraStation *station = IRCameraStation::instance();
    if (station) {
        station->start();
        const QString tag = QStringLiteral("resistance_test");
        station->clearSubscriptions(tag);

        IRCameraStation::Frame latest;
        QPointF pointPos(10.0, 10.0);
        QRectF boxRect(5.0, 5.0, 20.0, 20.0);
        if (station->tryGetLatestFrame(&latest) && latest.irImage.isNull() == false) {
            const QSize size = latest.irImage.size();
            pointPos = QPointF(size.width() / 2.0, size.height() / 2.0);
            boxRect = QRectF(pointPos.x() - 15.0, pointPos.y() - 15.0, 30.0, 30.0);
        }

        station->setPointSubscription(tag, QStringLiteral("P1"), pointPos);
        station->setBoxSubscription(tag, QStringLiteral("B1"), boxRect);

        auto frameConn = connect(station, &IRCameraStation::frameReady, this,
                                 [capture](const IRCameraStation::Frame &frame) {
                                     capture->frame = frame;
                                     capture->hasFrame = true;
                                 });
        auto pointConn = connect(station, &IRCameraStation::pointTemperatureReady, this,
                                 [capture](const IRCameraStation::PointResult &result) {
                                     capture->point = result;
                                     capture->hasPoint = true;
                                     if (capture->t0ms < 0) {
                                         capture->t0ms = result.timestampMs;
                                     }
                                     const double tSec = (result.timestampMs - capture->t0ms) / 1000.0;
                                     capture->tempTimes.push_back(tSec);
                                     capture->tempValues.push_back(result.temp);
                                 });
        auto boxConn = connect(station, &IRCameraStation::boxTemperatureReady, this,
                               [capture](const IRCameraStation::BoxResult &result) {
                                   capture->box = result;
                                   capture->hasBox = true;
                               });

        QMetaObject::Connection batchConn;
        if (m_runtime && m_runtime->rts()) {
            batchConn = connect(m_runtime->rts(), &RuntimeServices::batchReady, this,
                                [capture](const JYAlignedBatch &batch) {
                                    auto appendSeries = [capture](const JYDataPacket &packet, double sampleRate,
                                                                  QVector<double> &times, QVector<double> &values) {
                                        if (packet.channelCount <= 0) {
                                            return;
                                        }
                                        const int samples = packet.data.size() / packet.channelCount;
                                        if (samples <= 0) {
                                            return;
                                        }
                                        if (capture->t0ms < 0) {
                                            capture->t0ms = packet.timestampMs;
                                        }
                                        const double dtMs = 1000.0 / sampleRate;
                                        const double baseMs = static_cast<double>(packet.timestampMs - capture->t0ms);
                                        const double lastOffset = (samples - 1) * dtMs;
                                        for (int i = 0; i < samples; ++i) {
                                            const int idx = i * packet.channelCount;
                                            if (idx < 0 || idx >= packet.data.size()) {
                                                continue;
                                            }
                                            const double tMs = baseMs - (lastOffset - i * dtMs);
                                            if (tMs < 0.0) {
                                                continue;
                                            }
                                            times.push_back(tMs / 1000.0);
                                            values.push_back(packet.data[idx]);
                                        }
                                    };

                                    if (batch.packets.contains(JYDeviceKind::PXIe5322)) {
                                        appendSeries(batch.packets.value(JYDeviceKind::PXIe5322), 1000000.0,
                                                     capture->x5322, capture->y5322);
                                    }
                                    if (batch.packets.contains(JYDeviceKind::PXIe5323)) {
                                        appendSeries(batch.packets.value(JYDeviceKind::PXIe5323), 200000.0,
                                                     capture->x5323, capture->y5323);
                                    }
                                }, Qt::QueuedConnection);
        }

        QTimer::singleShot(1500, this, [=]() {
            station->clearSubscriptions(tag);
            disconnect(frameConn);
            disconnect(pointConn);
            disconnect(boxConn);
            if (batchConn) {
                disconnect(batchConn);
            }

            if (m_runtime && m_runtime->rts()) {
                QString stopError;
                m_runtime->rts()->pauseRun(2000, &stopError);
            }

            ComponentViewData view;
            view.id = item.componentRef;
            view.name = item.componentRef;
            if (capture->hasFrame) {
                view.thermalImage = capture->frame.irImage;
                view.thermalMatrix = capture->frame.temperatureMatrix;
                view.thermalMatrixSize = capture->frame.matrixSize;
            }

            QString tempHtml;
            if (capture->hasPoint) {
                tempHtml += QStringLiteral("<p>点温度：%1 °C</p>").arg(capture->point.temp, 0, 'f', 2);
            }
            if (capture->hasBox) {
                tempHtml += QStringLiteral("<p>框温度(min/avg/max)：%1 / %2 / %3 °C</p>")
                    .arg(capture->box.minTemp, 0, 'f', 2)
                    .arg(capture->box.avgTemp, 0, 'f', 2)
                    .arg(capture->box.maxTemp, 0, 'f', 2);
            }

            TPSResult result;
            QString error;
            plugin->execute(request, &result, &error);
            const double measured = result.metrics.value(QStringLiteral("measuredOhms")).toDouble();
            view.reportHtml = QStringLiteral(
                "<h3>电阻测试结果</h3>"
                "<p>状态：%1</p>"
                "<p>标称：%2 Ω</p>"
                "<p>实测：%3 Ω</p>"
                "<p>容差：%4 %%</p>%5")
                .arg(result.success ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                .arg(result.metrics.value(QStringLiteral("nominalOhms")).toDouble(), 0, 'f', 2)
                .arg(measured, 0, 'f', 2)
                .arg(result.metrics.value(QStringLiteral("tolerancePercent")).toDouble(), 0, 'f', 2)
                .arg(tempHtml);

            view.x = capture->tempTimes.isEmpty() ? QVector<double>{0.0} : capture->tempTimes;
            view.y = capture->tempValues.isEmpty() ? QVector<double>{measured} : capture->tempValues;
            view.x5322 = capture->x5322;
            view.y5322 = capture->y5322;
            view.x5323 = capture->x5323;
            view.y5323 = capture->y5323;

            updateComponent(view);
        });
    }
}

void FaultDiagnostic::buildWidgets()
{
    if (!ui) {
        return;
    }

    m_list = ui->listComponents;
    if (m_list) {
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    }

    if (ui->thermalContainer) {
        if (!ui->thermalContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->thermalContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_tempView = new IRCamera(ui->thermalContainer);
        m_tempView->setStationEnabled(false);

        if (auto *splitter = m_tempView->findChild<QSplitter *>(QStringLiteral("splitterMain"))) {
            splitter->setSizes({0, 1});
        }
        if (auto *leftPanel = m_tempView->findChild<QWidget *>(QStringLiteral("leftPanel"))) {
            leftPanel->setVisible(false);
        }

        ui->thermalContainer->layout()->addWidget(m_tempView);
    }

    if (ui->plotContainer) {
        if (!ui->plotContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->plotContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_plot = new UESTCQCustomPlot(ui->plotContainer);
        m_plot->setTimeAxisEnabled(false);
        m_plot->setAutoRangeEnabled(true);
        ui->plotContainer->layout()->addWidget(m_plot);
    }

    if (ui->reportContainer) {
        if (!ui->reportContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->reportContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_report = new QTextBrowser(ui->reportContainer);
        m_report->setOpenExternalLinks(false);
        ui->reportContainer->layout()->addWidget(m_report);
    }
}

void FaultDiagnostic::applyThemeQss()
{
    const QString theme = qApp ? qApp->property("atsTheme").toString().toLower() : QString();
    if (m_applyingQss) {
        return;
    }
    if (!theme.isEmpty() && theme == m_loadedTheme) {
        return;
    }
    const QString qssPath = (theme == QStringLiteral("light"))
        ? QStringLiteral(":/styles/faultdiagnostic_light.qss")
        : QStringLiteral(":/styles/faultdiagnostic_dark.qss");
    QFile file(qssPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_applyingQss = true;
        setStyleSheet(QString::fromUtf8(file.readAll()));
        m_applyingQss = false;
        if (!theme.isEmpty()) {
            m_loadedTheme = theme;
        }
    }
}

void FaultDiagnostic::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_components.size()) {
        return;
    }
    const auto &item = m_components[index];
    refreshThermal(item);
    refreshPlot(item);
    refreshReport(item);
}

void FaultDiagnostic::refreshThermal(const ComponentViewData &item)
{
    if (!m_tempView) {
        return;
    }
    if (!item.thermalImage.isNull() && !item.thermalMatrix.isEmpty() && item.thermalMatrixSize.isValid()) {
        m_tempView->setInputData(item.thermalImage, item.thermalMatrix, item.thermalMatrixSize);
    } else {
        m_tempView->setInputData(QImage(), {}, QSize());
    }
}

void FaultDiagnostic::refreshPlot(const ComponentViewData &item)
{
    if (!m_plot) {
        return;
    }
    if (!m_tempGraph) {
        m_tempGraph = m_plot->addStaticLine(QStringLiteral("温度"), item.x, item.y, QColor(52, 152, 219));
    } else {
        m_plot->updateStaticLine(m_tempGraph, item.x, item.y);
    }

    if (!m_graph5322) {
        m_graph5322 = m_plot->addStaticLine(QStringLiteral("5322"), item.x5322, item.y5322, QColor(46, 204, 113));
    } else {
        m_plot->updateStaticLine(m_graph5322, item.x5322, item.y5322);
    }

    if (!m_graph5323) {
        m_graph5323 = m_plot->addStaticLine(QStringLiteral("5323"), item.x5323, item.y5323, QColor(241, 196, 15));
    } else {
        m_plot->updateStaticLine(m_graph5323, item.x5323, item.y5323);
    }
}

void FaultDiagnostic::refreshReport(const ComponentViewData &item)
{
    if (!m_report) {
        return;
    }
    if (item.reportHtml.isEmpty()) {
        m_report->clear();
        return;
    }
    m_report->setHtml(item.reportHtml);
}
