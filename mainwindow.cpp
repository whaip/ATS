#include "mainwindow.h"
#include "logger.h"
#include "ui_mainwindow.h"
#include "logger.h"
#include "pagebuttonmanager.h"
#include "HDCamera/hdcamera.h"
#include "ComponentsDetect/componentsdetect.h"
#include "tool/lebalitemmanager.h"
#include "IODevices/DataCaptureCard/datacapturecard.h"
#include "IRCamera/ircamera.h"
#include "IODevices/DataGenerateCard/datageneratecard.h"
#include "IRCamera/ircamerastation.h"
#include "IODevices/JYDevices/jythreadmanager.h"
#include "IODevices/JYDevices/jydeviceworker.h"
#include "IODevices/JYDevices/jydeviceconfigutils.h"
#include "include/JY8902.h"
#include <QDebug>
#include "FaultDiagnostic/UI/faultdiagnostic.h"
#include "FaultDiagnostic/UI/configurationwindow.h"
#include "BoardManager/boardmanager.h"
#include <QHeaderView>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <algorithm>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_jyManager = new JYThreadManager(this);

    m_jyStates[JYDeviceKind::PXIe5711] = JYDeviceState::Closed;
    m_jyStates[JYDeviceKind::PXIe5322] = JYDeviceState::Closed;
    m_jyStates[JYDeviceKind::PXIe5323] = JYDeviceState::Closed;
    m_jyStates[JYDeviceKind::PXIe8902] = JYDeviceState::Closed;
    m_capture8902Config.measurementFunction = JY8902_DMM_MeasurementFunction::JY8902_DC_Volts;
    m_capture8902Config.range = JY8902_DMM_DC_VoltRange::JY8902_DC_Volt_Auto;

    auto statusLabelForKind = [this](JYDeviceKind kind) -> QLabel * {
        if (!ui) {
            return nullptr;
        }
        switch (kind) {
            case JYDeviceKind::PXIe5711:
                return ui->labelJY5711Status;
            case JYDeviceKind::PXIe5322:
                return ui->labelJY5322Status;
            case JYDeviceKind::PXIe5323:
                return ui->labelJY5323Status;
            case JYDeviceKind::PXIe8902:
                return ui->labelJY8902Status;
        }
        return nullptr;
    };

    auto updateOverview = [this]() {
        if (!ui || !ui->labelOverviewStatus) {
            return;
        }
        const auto s5711 = m_jyStates.value(JYDeviceKind::PXIe5711, JYDeviceState::Closed);
        const auto s5322 = m_jyStates.value(JYDeviceKind::PXIe5322, JYDeviceState::Closed);
        const auto s5323 = m_jyStates.value(JYDeviceKind::PXIe5323, JYDeviceState::Closed);
        const auto s8902 = m_jyStates.value(JYDeviceKind::PXIe8902, JYDeviceState::Closed);

        const bool anyFault = (s5711 == JYDeviceState::Faulted
                               || s5322 == JYDeviceState::Faulted
                               || s5323 == JYDeviceState::Faulted
                               || s8902 == JYDeviceState::Faulted);
        const bool allInited = (s5711 == JYDeviceState::Configured || s5711 == JYDeviceState::Armed || s5711 == JYDeviceState::Running)
                               && (s5322 == JYDeviceState::Configured || s5322 == JYDeviceState::Armed || s5322 == JYDeviceState::Running)
                               && (s5323 == JYDeviceState::Configured || s5323 == JYDeviceState::Armed || s5323 == JYDeviceState::Running)
                               && (s8902 == JYDeviceState::Configured || s8902 == JYDeviceState::Armed || s8902 == JYDeviceState::Running);
        const bool anyInited = (s5711 == JYDeviceState::Configured || s5711 == JYDeviceState::Armed || s5711 == JYDeviceState::Running)
                               || (s5322 == JYDeviceState::Configured || s5322 == JYDeviceState::Armed || s5322 == JYDeviceState::Running)
                               || (s5323 == JYDeviceState::Configured || s5323 == JYDeviceState::Armed || s5323 == JYDeviceState::Running)
                               || (s8902 == JYDeviceState::Configured || s8902 == JYDeviceState::Armed || s8902 == JYDeviceState::Running);

        QString text = QStringLiteral("未初始化");
        if (anyFault) {
            text = QStringLiteral("初始化失败");
        } else if (allInited) {
            text = QStringLiteral("已初始化");
        } else if (anyInited) {
            text = QStringLiteral("部分初始化");
        }
        ui->labelOverviewStatus->setText(text);
    };

    auto updateDeviceStatus = [this, statusLabelForKind, updateOverview](JYDeviceKind kind, JYDeviceState state, const QString &message) {
        m_jyStates[kind] = state;
        if (auto *label = statusLabelForKind(kind)) {
            QString text = QStringLiteral("状态：未初始化");
            if (state == JYDeviceState::Faulted) {
                text = message.isEmpty()
                           ? QStringLiteral("状态：初始化失败")
                           : QStringLiteral("状态：初始化失败(%1)").arg(message);
            } else if (state == JYDeviceState::Configured || state == JYDeviceState::Armed || state == JYDeviceState::Running) {
                text = QStringLiteral("状态：已初始化");
            } else if (state == JYDeviceState::Closed) {
                text = QStringLiteral("状态：未初始化");
            } else {
                text = QStringLiteral("状态：初始化中");
            }
            label->setText(text);
        }
        updateOverview();
    };

    auto attachWorker = [this, updateDeviceStatus](JYDeviceWorker *worker) {
        if (!worker) {
            return;
        }
        const JYDeviceKind kind = worker->kind();
        if (!m_jyWorkers.contains(kind)) {
            m_jyWorkers.insert(kind, worker);
        }
        connect(worker, &JYDeviceWorker::statusChanged, this,
                [updateDeviceStatus](JYDeviceKind kind, JYDeviceState state, const QString &message) {
                    updateDeviceStatus(kind, state, message);
                });
    };

    auto build532xConfig = [](JYDeviceKind kind, int channelCount) {
        JYDeviceConfig config;
        config.kind = kind;
        config.cfg532x.slotNumber = (kind == JYDeviceKind::PXIe5322) ? 5 : 3;
        config.cfg532x.channelCount = channelCount;
        config.cfg532x.sampleRate = (kind == JYDeviceKind::PXIe5322) ? 1000000.0 : 200000.0;
        config.cfg532x.samplesPerRead = 1024;
        config.cfg532x.timeoutMs = 1000;
        config.cfg532x.lowRange = -10.0;
        config.cfg532x.highRange = 10.0;
        config.cfg532x.bandwidth = 0;
        return config;
    };

    auto build5711Config = []() {
        JYDeviceConfig config;
        config.kind = JYDeviceKind::PXIe5711;
        config.cfg5711.channelCount = 1;
        config.cfg5711.sampleRate = 1000000.0;
        config.cfg5711.lowRange = -10.0;
        config.cfg5711.highRange = 10.0;
        JY5711WaveformConfig wf;
        wf.channel = 0;
        wf.type = PXIe5711_testtype::HighLevelWave;
        wf.amplitude = 0.0;
        wf.frequency = 0.0;
        wf.dutyCycle = 1.0;
        config.cfg5711.waveforms.push_back(wf);
        return config;
    };

    auto build8902Config = []() {
        JYDeviceConfig config;
        config.kind = JYDeviceKind::PXIe8902;
        config.cfg8902.sampleCount = 20;
        config.cfg8902.timeoutMs = 1000;
        config.cfg8902.measurementFunction = 0;
        config.cfg8902.range = -1;
        config.cfg8902.apertureTime = 0.02;
        config.cfg8902.triggerDelay = 0.1;
        return config;
    };

    auto updateOverviewStatus = [this]() {
        if (!ui || !ui->labelOverviewStatus) {
            return;
        }
        const QVector<JYDeviceKind> kinds = {
            JYDeviceKind::PXIe5711,
            JYDeviceKind::PXIe5322,
            JYDeviceKind::PXIe5323,
            JYDeviceKind::PXIe8902
        };
        bool anyFault = false;
        bool allInit = true;
        for (auto kind : kinds) {
            const JYDeviceState state = m_jyStates.value(kind, JYDeviceState::Closed);
            if (state == JYDeviceState::Faulted) {
                anyFault = true;
            }
            if (state != JYDeviceState::Configured
                && state != JYDeviceState::Armed
                && state != JYDeviceState::Running) {
                allInit = false;
            }
        }
        if (anyFault) {
            ui->labelOverviewStatus->setText(QStringLiteral("初始化失败"));
        } else if (allInit) {
            ui->labelOverviewStatus->setText(QStringLiteral("已初始化"));
        } else {
            ui->labelOverviewStatus->setText(QStringLiteral("未初始化"));
        }
    };

    auto ensureWorker = [this](JYDeviceKind kind) -> JYDeviceWorker * {
        if (!m_jyManager) {
            return nullptr;
        }
        if (m_jyWorkers.contains(kind)) {
            return m_jyWorkers.value(kind);
        }
        JYDeviceWorker *worker = nullptr;
        switch (kind) {
            case JYDeviceKind::PXIe5322:
            case JYDeviceKind::PXIe5323:
                worker = m_jyManager->create532xWorker(kind);
                break;
            case JYDeviceKind::PXIe5711:
                worker = m_jyManager->create5711Worker();
                break;
            case JYDeviceKind::PXIe8902:
                worker = m_jyManager->create8902Worker();
                break;
        }
        if (worker) {
            m_jyWorkers.insert(kind, worker);
            m_jyStates.insert(kind, worker->state());
        }
        return worker;
    };

    auto bindStatus = [this, updateOverviewStatus](JYDeviceWorker *worker, QLabel *label, JYDeviceKind kind) {
        if (!worker || !label) {
            return;
        }
        connect(worker, &JYDeviceWorker::statusChanged, this,
                [this, label, kind, updateOverviewStatus](JYDeviceKind, JYDeviceState state, const QString &message) {
                    m_jyStates[kind] = state;
                    label->setText(QStringLiteral("状态：%1").arg(jyDeviceStateText(state, message)));
                    updateOverviewStatus();
                });
    };

    if (ui->pagesStack && ui->pageButtonsLayout) {
        m_pageButtonManager = new PageButtonManager(ui->pagesStack, ui->pageButtonsLayout, this);
        ui->pagesStack->setCurrentIndex(0);

        if (ui->labelPageInfo) {
            ui->labelPageInfo->setText(QStringLiteral("共%1页").arg(ui->pagesStack->count()));
        }

        connect(ui->btnPagePrev, &QToolButton::clicked, this, [this]() {
            if (!ui->pagesStack) {
                return;
            }
            const int idx = ui->pagesStack->currentIndex();
            const int next = qMax(0, idx - 1);
            ui->pagesStack->setCurrentIndex(next);
        });
        connect(ui->btnPageNext, &QToolButton::clicked, this, [this]() {
            if (!ui->pagesStack) {
                return;
            }
            const int idx = ui->pagesStack->currentIndex();
            const int next = qMin(ui->pagesStack->count() - 1, idx + 1);
            ui->pagesStack->setCurrentIndex(next);
        });

        connect(ui->btnGo, &QPushButton::clicked, this, [this]() {
            if (!ui->pagesStack) {
                return;
            }
            bool ok = false;
            const int page = ui->editJumpPage ? ui->editJumpPage->text().toInt(&ok) : 0;
            if (!ok) {
                return;
            }
            const int idx = page - 1;
            if (idx < 0 || idx >= ui->pagesStack->count()) {
                return;
            }
            ui->pagesStack->setCurrentIndex(idx);

            if (ui->labelPageInfo) {
                ui->labelPageInfo->setText(QStringLiteral("共%1页").arg(ui->pagesStack->count()));
            }
        });
    }

    if (ui->pagesStack) {
        addPage(new HDCamera(), QStringLiteral("摄像头控制"), false);
        addPage(new ComponentsDetect(), QStringLiteral("元器件识别"), false);
        if (ui->labelPageInfo) {
            ui->labelPageInfo->setText(QStringLiteral("共%1页").arg(ui->pagesStack->count()));
        }
    }

    if (ui->navMaintain) {
        connect(ui->navMaintain, &QToolButton::clicked, this, [this]() {
            if (!ui || !ui->pagesStack) {
                return;
            }
            if (!m_maintainPage) {
                m_maintainPage = new LebalItemManager();
                m_maintainPageIndex = addPage(m_maintainPage, QStringLiteral("Maintain"), false);
            }
            if (m_maintainPageIndex >= 0 && m_maintainPageIndex < ui->pagesStack->count()) {
                ui->pagesStack->setCurrentIndex(m_maintainPageIndex);
            }
        });
    }

    if (ui->navSetting) {
        connect(ui->navSetting, &QToolButton::clicked, this, [this]() {
            if (!ui || !ui->pagesStack) {
                return;
            }
            if (!m_configurationPage) {
                m_configurationPage = new ConfigurationWindow();
                m_configurationPageIndex = addPage(m_configurationPage, QStringLiteral("TestSequence"), false);
            }
            if (m_configurationPageIndex >= 0 && m_configurationPageIndex < ui->pagesStack->count()) {
                ui->pagesStack->setCurrentIndex(m_configurationPageIndex);
            }
        });
    }

    if (ui->btnDataCaptureCard) {
        connect(ui->btnDataCaptureCard, &QToolButton::clicked, this, [this, attachWorker]() {
            if (!ui || !ui->pagesStack) {
                return;
            }
            if (!m_dataCapturePage) {
                m_dataCapturePage = new DataCaptureCard();
                m_dataCapturePage->setJYThreadManager(m_jyManager);
                m_dataCapturePageIndex = addPage(m_dataCapturePage, QStringLiteral("DataCapture"), false);

                connect(m_dataCapturePage, &DataCaptureCard::config8902Changed, this,
                        [this](const JY8902Config &config) {
                            m_capture8902Config = config;
                        });

                connect(m_dataCapturePage, &DataCaptureCard::startRequestedWithChannels, this,
                    [this, attachWorker](const QSet<int> &channels5322, const QSet<int> &channels5323, const QSet<int> &channels8902) {
                            if (!m_jyManager) {
                                return;
                            }

                            Logger::log(QStringLiteral("Capture start: 5322=%1 5323=%2 8902=%3")
                                            .arg(channels5322.size())
                                            .arg(channels5323.size())
                                            .arg(channels8902.size()),
                                        Logger::Level::Info);

                            auto buildCapture532x = [](JYDeviceKind kind, int channelCount) {
                                JYDeviceConfig config;
                                config.kind = kind;
                                config.cfg532x.slotNumber = (kind == JYDeviceKind::PXIe5322) ? 5 : 3;
                                config.cfg532x.channelCount = channelCount;
                                config.cfg532x.sampleRate = (kind == JYDeviceKind::PXIe5322) ? 1000000.0 : 200000.0;
                                config.cfg532x.samplesPerRead = 1000;
                                config.cfg532x.timeoutMs = -1;
                                config.cfg532x.lowRange = -10.0;
                                config.cfg532x.highRange = 10.0;
                                config.cfg532x.bandwidth = 0;
                                return config;
                            };

                            const int max5322 = channels5322.isEmpty() ? 0 : (*std::max_element(channels5322.begin(), channels5322.end()) + 1);
                            const int max5323 = channels5323.isEmpty() ? 0 : (*std::max_element(channels5323.begin(), channels5323.end()) + 1);

                            if (!channels5322.isEmpty()) {
                                auto *worker = m_jyWorkers.value(JYDeviceKind::PXIe5322, nullptr);
                                if (!worker) {
                                    worker = m_jyManager->create532xWorker(JYDeviceKind::PXIe5322);
                                    attachWorker(worker);
                                }
                                if (worker) {
                                    const int count = qMax(1, qMin(max5322, 16));
                                    worker->postConfigure(buildCapture532x(JYDeviceKind::PXIe5322, count));
                                    worker->postStart();
                                    worker->postTrigger();
                                }
                            }

                            if (!channels5323.isEmpty()) {
                                auto *worker = m_jyWorkers.value(JYDeviceKind::PXIe5323, nullptr);
                                if (!worker) {
                                    worker = m_jyManager->create532xWorker(JYDeviceKind::PXIe5323);
                                    attachWorker(worker);
                                }
                                if (worker) {
                                    const int count = qMax(1, qMin(max5323, 32));
                                    worker->postConfigure(buildCapture532x(JYDeviceKind::PXIe5323, count));
                                    worker->postStart();
                                    worker->postTrigger();
                                }
                            }

                            if (!channels8902.isEmpty()) {
                                auto *worker8902 = m_jyWorkers.value(JYDeviceKind::PXIe8902, nullptr);
                                if (!worker8902) {
                                    worker8902 = m_jyManager->create8902Worker();
                                    attachWorker(worker8902);
                                }
                                if (worker8902) {
                                    JYDeviceConfig config;
                                    config.kind = JYDeviceKind::PXIe8902;
                                    config.cfg8902 = m_capture8902Config;
                                    config.cfg8902.sampleCount = 1;
                                    config.cfg8902.timeoutMs = 1000;
                                    worker8902->postConfigure(config);
                                    worker8902->postStart();
                                    worker8902->postTrigger();
                                }
                            }
                        });

                connect(m_dataCapturePage, &DataCaptureCard::stopRequested, this, [this]() {
                    if (!m_jyManager) {
                        return;
                    }
                    Logger::log(QStringLiteral("Capture stop"), Logger::Level::Info);
                    if (auto *w = m_jyWorkers.value(JYDeviceKind::PXIe5322, nullptr)) {
                        w->postStop();
                    }
                    if (auto *w = m_jyWorkers.value(JYDeviceKind::PXIe5323, nullptr)) {
                        w->postStop();
                    }
                    if (auto *w = m_jyWorkers.value(JYDeviceKind::PXIe8902, nullptr)) {
                        w->postStop();
                    }
                });
            }
            if (m_dataCapturePageIndex >= 0 && m_dataCapturePageIndex < ui->pagesStack->count()) {
                ui->pagesStack->setCurrentIndex(m_dataCapturePageIndex);
            }
        });
    }

    if (ui->navIRCamera) {
        connect(ui->navIRCamera, &QToolButton::clicked, this, [this]() {
            if (!ui || !ui->pagesStack) {
                return;
            }
            if (!m_irCameraPage) {
                m_irCameraPage = new IRCamera();
                m_irCameraPageIndex = addPage(m_irCameraPage, QStringLiteral("IRCamera"), false);
            }
            if (m_irCameraPageIndex >= 0 && m_irCameraPageIndex < ui->pagesStack->count()) {
                ui->pagesStack->setCurrentIndex(m_irCameraPageIndex);
            }

            if (m_irCameraPage) {
                m_irCameraPage->setStationEnabled(IRCameraStation::instance()->isRunning());
            }
        });
    }

    if (ui->btnDataGenerateCard) {
        connect(ui->btnDataGenerateCard, &QToolButton::clicked, this, [this]() {
            if (!ui || !ui->pagesStack) {
                return;
            }
            if (!m_dataGeneratePage) {
                m_dataGeneratePage = new DataGenerateCard();
                m_dataGeneratePage->setJYThreadManager(m_jyManager);
                m_dataGeneratePageIndex = addPage(m_dataGeneratePage, QStringLiteral("DataGenerate"), false);
            }
            if (m_dataGeneratePageIndex >= 0 && m_dataGeneratePageIndex < ui->pagesStack->count()) {
                ui->pagesStack->setCurrentIndex(m_dataGeneratePageIndex);
            }
        });
    }
    
    if (ui->FaultDiagnostic) {
        connect(ui->FaultDiagnostic, &QToolButton::clicked, this, [this]() {
            if (!ui || !ui->pagesStack) {
                return;
            }
            if (!m_faultDiagnosticPage) {
                m_faultDiagnosticPage = new FaultDiagnostic();
                m_faultDiagnosticPageIndex = addPage(m_faultDiagnosticPage, QStringLiteral("FaultDiagnostic"), false);
            }
            if (m_faultDiagnosticPageIndex >= 0 && m_faultDiagnosticPageIndex < ui->pagesStack->count()) {
                ui->pagesStack->setCurrentIndex(m_faultDiagnosticPageIndex);
            }
        });
    }

    if (ui->tabBoardManager) {
        connect(ui->tabBoardManager, &QToolButton::clicked, this, [this]() {
            if (!ui || !ui->pagesStack) {
                return;
            }
            if (!m_boardManagerPage) {
                m_boardManagerPage = new BoardManager();
                m_boardManagerPageIndex = addPage(m_boardManagerPage, QStringLiteral("BoardManager"), false);
            }
            if (m_boardManagerPageIndex >= 0 && m_boardManagerPageIndex < ui->pagesStack->count()) {
                ui->pagesStack->setCurrentIndex(m_boardManagerPageIndex);
            }
        });
    }

    if (ui->btnInitJY5711) {
        connect(ui->btnInitJY5711, &QPushButton::clicked, this, [this, attachWorker, build5711Config]() {
            if (!m_jyManager) {
                return;
            }
            if (!m_jyWorkers.contains(JYDeviceKind::PXIe5711)) {
                auto *worker = m_jyManager->create5711Worker();
                attachWorker(worker);
            }
            if (ui && ui->labelJY5711Status) {
                ui->labelJY5711Status->setText(QStringLiteral("状态：初始化中"));
            }
            if (auto *worker = m_jyWorkers.value(JYDeviceKind::PXIe5711, nullptr)) {
                worker->postConfigure(build5711Config());
            }
        });
    }

    if (ui->btnInitJY5322) {
        connect(ui->btnInitJY5322, &QPushButton::clicked, this, [this, attachWorker, build532xConfig]() {
            if (!m_jyManager) {
                return;
            }
            if (!m_jyWorkers.contains(JYDeviceKind::PXIe5322)) {
                auto *worker = m_jyManager->create532xWorker(JYDeviceKind::PXIe5322);
                attachWorker(worker);
            }
            if (ui && ui->labelJY5322Status) {
                ui->labelJY5322Status->setText(QStringLiteral("状态：初始化中"));
            }
            if (auto *worker = m_jyWorkers.value(JYDeviceKind::PXIe5322, nullptr)) {
                worker->postConfigure(build532xConfig(JYDeviceKind::PXIe5322, 16));
            }
        });
    }

    if (ui->btnInitJY5323) {
        connect(ui->btnInitJY5323, &QPushButton::clicked, this, [this, attachWorker, build532xConfig]() {
            if (!m_jyManager) {
                return;
            }
            if (!m_jyWorkers.contains(JYDeviceKind::PXIe5323)) {
                auto *worker = m_jyManager->create532xWorker(JYDeviceKind::PXIe5323);
                attachWorker(worker);
            }
            if (ui && ui->labelJY5323Status) {
                ui->labelJY5323Status->setText(QStringLiteral("状态：初始化中"));
            }
            if (auto *worker = m_jyWorkers.value(JYDeviceKind::PXIe5323, nullptr)) {
                worker->postConfigure(build532xConfig(JYDeviceKind::PXIe5323, 32));
            }
        });
    }

    if (ui->btnInitJY8902) {
        connect(ui->btnInitJY8902, &QPushButton::clicked, this, [this, attachWorker, build8902Config]() {
            if (!m_jyManager) {
                return;
            }
            if (!m_jyWorkers.contains(JYDeviceKind::PXIe8902)) {
                auto *worker = m_jyManager->create8902Worker();
                attachWorker(worker);
            }
            if (ui && ui->labelJY8902Status) {
                ui->labelJY8902Status->setText(QStringLiteral("状态：初始化中"));
            }
            if (auto *worker = m_jyWorkers.value(JYDeviceKind::PXIe8902, nullptr)) {
                worker->postConfigure(build8902Config());
            }
        });
    }

    auto openIrCameraPage = [this]() {
        if (!ui || !ui->pagesStack) {
            return;
        }
        if (!m_irCameraPage) {
            m_irCameraPage = new IRCamera();
            m_irCameraPageIndex = addPage(m_irCameraPage, QStringLiteral("IRCamera"), false);
        }
        if (m_irCameraPageIndex >= 0 && m_irCameraPageIndex < ui->pagesStack->count()) {
            ui->pagesStack->setCurrentIndex(m_irCameraPageIndex);
        }
        if (m_irCameraPage) {
            m_irCameraPage->setStationEnabled(IRCameraStation::instance()->isRunning());
        }
    };

    if (ui->btnInfraredDetail) {
        connect(ui->btnInfraredDetail, &QToolButton::clicked, this, [openIrCameraPage]() {
            openIrCameraPage();
        });
    }

    if (ui->btnInitInfrared) {
        connect(ui->btnInitInfrared, &QPushButton::clicked, this, [this]() {
            IRCameraStation::instance()->start();
            m_irStationRunning = IRCameraStation::instance()->isRunning();
            if (ui->labelInfraredStatus) {
                ui->labelInfraredStatus->setText(m_irStationRunning
                                                  ? QStringLiteral("状态：运行中")
                                                  : QStringLiteral("状态：初始化失败"));
            }
            if (m_irCameraPage) {
                m_irCameraPage->setStationEnabled(m_irStationRunning);
            }
        });
    }

    if (ui->labelInfraredStatus) {
        connect(IRCameraStation::instance(), &IRCameraStation::stationStatus, this, [this](const QString &text) {
            if (!ui || !ui->labelInfraredStatus) {
                return;
            }
            QString status = text;
            if (text.contains(QStringLiteral("started"), Qt::CaseInsensitive)) {
                status = QStringLiteral("运行中");
            } else if (text.contains(QStringLiteral("stopped"), Qt::CaseInsensitive)) {
                status = QStringLiteral("已停止");
            } else if (text.contains(QStringLiteral("failed"), Qt::CaseInsensitive)) {
                status = QStringLiteral("初始化失败");
            }
            ui->labelInfraredStatus->setText(QStringLiteral("状态：%1").arg(status));
        });
    }

    if (auto *table = ui->logTable) {
        table->setColumnWidth(0, 160);
        table->setColumnWidth(1, 90);
        table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->setVisible(false);

        if (auto *dispatcher = Logger::dispatcher()) {
            connect(dispatcher, &LogDispatcher::logAdded, this,
                    [this](const QString &time, const QString &level, const QString &message) {
                        auto *t = ui->logTable;
                        if (!t) {
                            return;
                        }
                        const int row = t->rowCount();
                        t->insertRow(row);
                        t->setItem(row, 0, new QTableWidgetItem(time));
                        t->setItem(row, 1, new QTableWidgetItem(level));
                        t->setItem(row, 2, new QTableWidgetItem(message));
                        t->scrollToBottom();
                    });
        }
    }

    qInfo() << "Application started";
    QToolBar *toolbar = addToolBar(tr("Style"));
}

MainWindow::~MainWindow()
{
    delete ui;
}

int MainWindow::addPage(QWidget *page, const QString &pageName, bool switchToPage)
{
    if (!ui || !ui->pagesStack || !page) {
        return -1;
    }

    if (!pageName.isEmpty()) {
        page->setWindowTitle(pageName);
        if (page->objectName().isEmpty()) {
            page->setObjectName(pageName);
        }
    }

    const int index = ui->pagesStack->addWidget(page);
    if (switchToPage && index >= 0) {
        ui->pagesStack->setCurrentIndex(index);
    }

    if (ui->labelPageInfo) {
        ui->labelPageInfo->setText(QStringLiteral("共%1页").arg(ui->pagesStack->count()));
    }

    if (m_pageButtonManager) {
        m_pageButtonManager->rebuild();
    }

    return index;
}

int MainWindow::addEmptyPage(const QString &pageName, bool switchToPage)
{
    auto *page = new QWidget();
    return addPage(page, pageName, switchToPage);
}
