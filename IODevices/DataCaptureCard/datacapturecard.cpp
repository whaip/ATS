#include "datacapturecard.h"
#include "ui_datacapturecard.h"
#include "waveformdatamanager.h"
#include "../uestcqcustomplot.h"
#include "../JYDevices/jythreadmanager.h"
#include "../../include/JY8902.h"
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

DataCaptureCard::DataCaptureCard(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DataCaptureCard)
{
    ui->setupUi(this);
    // 波形管理器负责把多设备采样统一整理成适合绘图的数据视图。
    m_waveformManager = new WaveformDataManager(this);
    m_waveformManager->setRetentionSeconds(m_displayRetentionSeconds);
    m_waveformManager->setStoragePointBudget(qMax(1000, m_displayMaxPoints * 6));
    connect(m_waveformManager, &WaveformDataManager::viewReady, this, [this](const WaveformViewFrame &frame) {
        if (!m_plot || m_useTestData) {
            return;
        }
        m_lastFrameLatestX = frame.latestX;

        QHash<QString, WaveformViewSeries> viewByKey;
        for (const auto &series : frame.series) {
            viewByKey.insert(series.key, series);
        }

        QVector<QCPGraph *> graphs;
        QVector<QVector<double>> xBuffers;
        QVector<QVector<double>> yBuffers;
        graphs.reserve(m_channels.size());
        xBuffers.reserve(m_channels.size());
        yBuffers.reserve(m_channels.size());

        for (auto &slot : m_channels) {
            if (!slot.graph || !slot.check || !slot.check->isChecked()) {
                continue;
            }

            const QString key = WaveformDataManager::seriesKey(slot.kind, slot.channel);
            const auto it = viewByKey.constFind(key);
            graphs.push_back(slot.graph);
            if (it == viewByKey.cend()) {
                xBuffers.push_back({});
                yBuffers.push_back({});
                slot.buffer.clear();
            } else {
                xBuffers.push_back(it->x);
                yBuffers.push_back(it->y);
                slot.buffer = it->y;
            }
        }

        m_updatingPlotView = true;
        m_plot->updateRealTimeSeries(graphs, xBuffers, yBuffers, true);
        m_updatingPlotView = false;
    }, Qt::QueuedConnection);
    m_viewRefreshTimer = new QTimer(this);
    m_viewRefreshTimer->setInterval(50);
    connect(m_viewRefreshTimer, &QTimer::timeout, this, [this]() {
        if (m_useTestData || !m_viewRefreshPending) {
            return;
        }
        m_viewRefreshPending = false;
        flushAlignedWindowToPlot();
    });

    m_8902Config.measurementFunction = JY8902_DMM_MeasurementFunction::JY8902_DC_Volts;
    m_8902Config.range = JY8902_DMM_DC_VoltRange::JY8902_DC_Volt_Auto;

    buildLayout();
    initPlotLines();
}

DataCaptureCard::~DataCaptureCard()
{
    delete ui;
}

void DataCaptureCard::buildLayout()
{
    // 将 .ui 中的控件指针接入成员变量，并生成各设备的通道卡片。
    m_plot = ui->plotView;
    m_portScroll = ui->scrollChannels;
    m_portContainer = ui->channelsContainer;
    m_portLayout = ui->channelsLayout;
    m_btnStartStop = ui->btnStartStop;
    m_labelCaptureStatus = ui->labelCaptureStatus;

    if (m_labelCaptureStatus) {
        m_labelCaptureStatus->setText(QStringLiteral("未采集"));
        m_labelCaptureStatus->setProperty("captureStatus", true);
    }

    applyThemeQss();

    auto *label5322 = ui->label5322Title;
    if (label5322) {
        label5322->setProperty("groupTitle", true);
    }
    auto *layout5322 = ui->channels5322Layout;
    for (int ch = 0; ch < 16; ++ch) {
        if (layout5322) {
            layout5322->addWidget(createChannelCard(JYDeviceKind::PXIe5322, ch, QColor(31, 119, 180)));
        }
    }

    auto *label5323 = ui->label5323Title;
    if (label5323) {
        label5323->setProperty("groupTitle", true);
    }
    auto *layout5323 = ui->channels5323Layout;
    for (int ch = 0; ch < 32; ++ch) {
        if (layout5323) {
            layout5323->addWidget(createChannelCard(JYDeviceKind::PXIe5323, ch, QColor(255, 127, 14)));
        }
    }

    auto *label8902 = ui->label8902Title;
    if (label8902) {
        label8902->setProperty("groupTitle", true);
    }
    auto *layout8902 = ui->channels8902Layout;
    if (layout8902) {
        layout8902->addWidget(createChannelCard(JYDeviceKind::PXIe8902, 0, QColor(46, 204, 113)));
    }
    if (m_portLayout) {
        m_portLayout->addStretch(1);
    }

    m_simTimer = new QTimer(this);
    m_simTimer->setInterval(50);
    connect(m_simTimer, &QTimer::timeout, this, [this]() {
        if (m_manager && !m_useTestData) {
            return;
        }
        if (m_graphs.isEmpty()) {
            return;
        }

        const int maxPoints = 600;
        for (int i = 0; i < m_buffers.size(); ++i) {
            const double base = std::sin((m_sampleIndex + i * 40) * 0.05);
            const double noise = (QRandomGenerator::global()->generateDouble() * 0.10) - 0.05;
            const double value = base + noise;

            auto &buf = m_buffers[i];
            if (buf.size() >= maxPoints) {
                buf.remove(0, buf.size() - maxPoints + 1);
            }
            buf.push_back(value);
        }

        m_plot->updateRealTimeLines(m_graphs, m_buffers);
        ++m_sampleIndex;
    });

    connect(m_btnStartStop, &QPushButton::clicked, this, [this]() {
        if (!m_running) {
            const auto selected5322 = selectedChannels(JYDeviceKind::PXIe5322);
            const auto selected5323 = selectedChannels(JYDeviceKind::PXIe5323);
            const auto selected8902 = selectedChannels(JYDeviceKind::PXIe8902);
            if (m_manager) {
                if (!selected5322.isEmpty() && !m_manager->isDeviceInitialized(JYDeviceKind::PXIe5322)) {
                    QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("JY5322 未初始化，无法开始采集"));
                    return;
                }
                if (!selected5323.isEmpty() && !m_manager->isDeviceInitialized(JYDeviceKind::PXIe5323)) {
                    QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("JY5323 未初始化，无法开始采集"));
                    return;
                }
                if (!selected8902.isEmpty() && !m_manager->isDeviceInitialized(JYDeviceKind::PXIe8902)) {
                    QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("JY8902 未初始化，无法开始采集"));
                    return;
                }
            }
            setRunning(true);
            emit startRequested();
            emit startRequestedWithChannels(selected5322, selected5323, selected8902);
        } else {
            setRunning(false);
            emit stopRequested();
        }
    });

    setRunning(false);
}

void DataCaptureCard::initPlotLines()
{
    if (!m_plot) {
        return;
    }
    // 绘图窗口以保留时长为基础，后续再按当前勾选通道动态裁剪显示内容。
    m_plot->setDisplayWindowSeconds(m_displayRetentionSeconds);
    if (m_waveformManager) {
        m_waveformManager->setStoragePointBudget(qMax(1000, m_plot->plotAreaWidth() * 6));
    }
    m_graphs.clear();
    m_buffers.clear();
    rebuildActiveGraphs();
    connect(m_plot->xAxis, qOverload<const QCPRange &>(&QCPAxis::rangeChanged), this,
            [this](const QCPRange &) {
                if (m_useTestData || m_updatingPlotView) {
                    return;
                }
                if (!m_running) {
                    flushAlignedWindowToPlot();
                    return;
                }
                m_viewRefreshPending = true;
            }, Qt::UniqueConnection);
}

QFrame *DataCaptureCard::createChannelCard(JYDeviceKind kind, int channel, const QColor &color)
{
    Q_UNUSED(color)
    auto *card = new QFrame(m_portContainer);
    card->setFrameShape(QFrame::StyledPanel);
    card->setProperty("channelCard", true);
    card->installEventFilter(this);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);

    auto *titleRow = new QHBoxLayout();
    auto *dot = new QLabel(card);
    dot->setFixedSize(10, 10);
    if (kind == JYDeviceKind::PXIe5322) {
        dot->setObjectName(QStringLiteral("channelDot5322"));
    } else if (kind == JYDeviceKind::PXIe5323) {
        dot->setObjectName(QStringLiteral("channelDot5323"));
    } else {
        dot->setObjectName(QStringLiteral("channelDot8902"));
    }

    const QString name = QStringLiteral("CH%1").arg(channel, 2, 10, QLatin1Char('0'));
    auto *title = new QLabel(name, card);
    title->setObjectName(QStringLiteral("channelTitle"));

    auto *check = new QCheckBox(QStringLiteral("订阅"), card);

    titleRow->addWidget(dot);
    titleRow->addSpacing(6);
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    titleRow->addWidget(check);

    auto *summary = new QLabel(QStringLiteral("未配置"), card);
    summary->setObjectName(QStringLiteral("channelSummary"));

    auto *status = new QLabel(QStringLiteral("未采集"), card);
    status->setObjectName(QStringLiteral("channelStatus"));
    m_portStatusLabels.push_back(status);

    layout->addLayout(titleRow);
    layout->addWidget(summary);
    layout->addWidget(status);

    ChannelSlot slot;
    slot.kind = kind;
    slot.channel = channel;
    slot.name = name;
    slot.frame = card;
    slot.check = check;
    slot.summary = summary;
    slot.status = status;
    m_channels.push_back(slot);

    updateChannelSummary(m_channels.back());

    connect(check, &QCheckBox::toggled, this, [this, idx = m_channels.size() - 1](bool on) {
        if (idx < 0 || idx >= m_channels.size()) {
            return;
        }
        updateChannelStatus(m_channels[idx], on);
        rebuildActiveGraphs();
        m_viewRefreshPending = true;
    });

    return card;
}

bool DataCaptureCard::eventFilter(QObject *watched, QEvent *event)
{
    if (!event || event->type() != QEvent::MouseButtonDblClick) {
        return QWidget::eventFilter(watched, event);
    }

    for (auto &slot : m_channels) {
        if (slot.frame == watched && slot.kind == JYDeviceKind::PXIe8902) {
            open8902ConfigDialog();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void DataCaptureCard::applyThemeQss()
{
    const QString theme = qApp ? qApp->property("atsTheme").toString().toLower() : QString();
    if (m_applyingQss) {
        return;
    }
    if (!theme.isEmpty() && theme == m_loadedTheme) {
        return;
    }
    const QString qssPath = (theme == QStringLiteral("light"))
        ? QStringLiteral(":/styles/datacapturecard_light.qss")
        : QStringLiteral(":/styles/datacapturecard_dark.qss");
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

void DataCaptureCard::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event) {
        return;
    }
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        if (!m_applyingQss) {
            applyThemeQss();
        }
    }
}

void DataCaptureCard::setRunning(bool running)
{
    m_running = running;
    if (m_btnStartStop) {
        m_btnStartStop->setText(running ? QStringLiteral("停止采集") : QStringLiteral("开始采集"));
    }

    if (m_labelCaptureStatus) {
        m_labelCaptureStatus->setText(running ? QStringLiteral("等待数据...") : QStringLiteral("未采集"));
    }

    for (auto &slot : m_channels) {
        if (!slot.status) {
            continue;
        }
        if (running && slot.check && slot.check->isChecked()) {
            slot.status->setText(QStringLiteral("采集中"));
        } else {
            slot.status->setText(QStringLiteral("未采集"));
        }
    }

    if (m_simTimer) {
        if (running) {
            m_simTimer->start();
        } else {
            m_simTimer->stop();
        }
    }
    if (m_viewRefreshTimer) {
        if (!m_useTestData) {
            m_viewRefreshTimer->start();
            if (running) {
                m_viewRefreshPending = true;
            }
        } else {
            m_viewRefreshTimer->stop();
            m_viewRefreshPending = false;
        }
    }
    if (running && m_waveformManager && !m_useTestData) {
        m_waveformManager->resetForCapture(0);
        m_lastFrameLatestX = 0.0;
    } else if (!running && m_plot && !m_useTestData) {
        flushAlignedWindowToPlot();
    }
}
qint64 DataCaptureCard::plotIntervalUsForSelection(const QSet<int> &channels5322,
                                                   const QSet<int> &channels5323,
                                                   const QSet<int> &channels8902) const
{
    double rateHz = 0.0;
    if (!channels5322.isEmpty()) {
        rateHz = qMax(rateHz, 1000000.0);
    }
    if (!channels5323.isEmpty()) {
        rateHz = qMax(rateHz, 200000.0);
    }
    if (!channels8902.isEmpty()) {
        rateHz = qMax(rateHz, 60.0);
    }
    if (rateHz <= 0.0) {
        return 50000;
    }
    return static_cast<qint64>(qMax(1.0, 1000000.0 / rateHz));
}

void DataCaptureCard::setJYThreadManager(JYThreadManager *manager)
{
    m_manager = manager;
    m_useTestData = (m_manager == nullptr);
    // 接入真实线程管理器后，采集页改为监听 pipeline 的对齐结果，不再使用模拟数据。
    if (m_simTimer) {
        if (m_manager) {
            m_simTimer->stop();
        } else if (m_running) {
            m_simTimer->start();
        }
    }
    if (m_manager && m_manager->pipeline()) {
        JYDataAligner::Settings alignSettings;
        alignSettings.windowMs = 3000;
        alignSettings.maxAgeMs = 5000;
        m_manager->pipeline()->setAlignSettings(alignSettings);

        connect(m_manager->pipeline(), &JYDataPipeline::alignedBatchReady,
                this, &DataCaptureCard::onAlignedBatch, Qt::QueuedConnection);
        connect(m_manager->pipeline(), &JYDataPipeline::packetIngested, this,
                [this](JYDeviceKind kind, int channelCount, int dataSize, qint64 timestampMs) {
                    if (m_labelCaptureStatus) {
                        const int samples = channelCount > 0 ? (dataSize / channelCount) : 0;
                        m_labelCaptureStatus->setText(QStringLiteral("收到包: %1, %2ch x %3 @%4ms")
                                                       .arg(kindLabel(kind))
                                                       .arg(channelCount)
                                                       .arg(samples)
                                                       .arg(timestampMs));
                    }
                }, Qt::QueuedConnection);
        connect(m_manager->pipeline(), &JYDataPipeline::packetRejected, this,
                [this](JYDeviceKind kind, const QString &reason) {
                    if (m_labelCaptureStatus) {
                        m_labelCaptureStatus->setText(QStringLiteral("丢包: %1 (%2)")
                                                       .arg(kindLabel(kind), reason));
                    }
                }, Qt::QueuedConnection);
    }
}

int DataCaptureCard::deviceIndex(JYDeviceKind kind) const
{
    if (kind == JYDeviceKind::PXIe5322) return 0;
    if (kind == JYDeviceKind::PXIe5323) return 1;
    return -1;
}

void DataCaptureCard::onAlignedBatch(const JYAlignedBatch &batch)
{
    if (m_activeGraphs.isEmpty()) {
        return;
    }

    // 一批对齐数据可能同时包含 5322、5323、8902，不在这里区分来源，统一进入显示链路。

    ++m_batchCount;
    m_lastBatchTs = batch.timestampMs;
    if (m_waveformManager && m_batchCount == 1) {
        m_waveformManager->setEpoch(static_cast<quint64>(batch.timestampMs));
    }
    if (m_labelCaptureStatus) {
        QStringList parts;
        for (auto it = batch.packets.begin(); it != batch.packets.end(); ++it) {
            const auto &packet = it.value();
            const int samples = packet.channelCount > 0 ? (packet.data.size() / packet.channelCount) : 0;
            parts.push_back(QStringLiteral("%1: %2ch x %3")
                                .arg(kindLabel(it.key()))
                                .arg(packet.channelCount)
                                .arg(samples));
        }
        const QString detail = parts.isEmpty() ? QStringLiteral("无有效数据") : parts.join(QStringLiteral(" | "));
        m_labelCaptureStatus->setText(QStringLiteral("批次%1 @%2ms: %3")
                                       .arg(m_batchCount)
                                       .arg(m_lastBatchTs)
                                       .arg(detail));
    }

    if (m_waveformManager) {
        m_waveformManager->appendAlignedBatch(batch);
        m_viewRefreshPending = true;
    } else {
        for (auto it = batch.packets.begin(); it != batch.packets.end(); ++it) {
            appendPacketToBuffers(it.key(), it.value());
        }
        flushAlignedWindowToPlot();
    }
    ++m_sampleIndex;
}

int DataCaptureCard::windowSamples(JYDeviceKind kind) const
{
    if (kind == JYDeviceKind::PXIe5322) {
        return static_cast<int>(m_windowSeconds * 1000000.0);
    }
    if (kind == JYDeviceKind::PXIe5323) {
        return static_cast<int>(m_windowSeconds * 200000.0);
    }
    if (kind == JYDeviceKind::PXIe8902) {
        return static_cast<int>(m_windowSeconds * 60.0);
    }
    return 0;
}

int DataCaptureCard::retentionSamples(JYDeviceKind kind) const
{
    if (kind == JYDeviceKind::PXIe5322) {
        return static_cast<int>(m_displayRetentionSeconds * 1000000.0);
    }
    if (kind == JYDeviceKind::PXIe5323) {
        return static_cast<int>(m_displayRetentionSeconds * 200000.0);
    }
    if (kind == JYDeviceKind::PXIe8902) {
        return static_cast<int>(m_displayRetentionSeconds * 60.0);
    }
    return 0;
}

void DataCaptureCard::appendPacketToBuffers(JYDeviceKind kind, const JYDataPacket &packet)
{
    if (packet.channelCount <= 0 || packet.data.isEmpty()) {
        return;
    }

    // 原始包是按“采样点 * 通道数”交织存储，这里按通道拆开后续再做窗口裁剪。

    auto &buffers = m_captureBuffers[kind];
    if (buffers.size() != packet.channelCount) {
        buffers.clear();
        buffers.resize(packet.channelCount);
    }

    const int samples = packet.data.size() / packet.channelCount;
    if (samples <= 0) {
        return;
    }

    const QSet<int> selected = selectedChannels(kind);
    if (selected.isEmpty()) {
        return;
    }

    for (int ch = 0; ch < packet.channelCount; ++ch) {
        if (!selected.contains(ch)) {
            continue;
        }
        auto &buf = buffers[ch];
        buf.reserve(buf.size() + samples);
        for (int s = 0; s < samples; ++s) {
            const int index = s * packet.channelCount + ch;
            if (index < 0 || index >= packet.data.size()) {
                continue;
            }
            buf.push_back(packet.data[index]);
        }

        const int target = windowSamples(kind);
        if (target > 0 && buf.size() > target * 2) {
            buf.remove(0, buf.size() - target);
        }
    }
}

bool DataCaptureCard::hasWindowReady(const QSet<int> &channels, JYDeviceKind kind) const
{
    Q_UNUSED(channels)
    Q_UNUSED(kind)
    if (m_waveformManager) {
        return true;
    }
    if (channels.isEmpty()) {
        return true;
    }
    const int target = windowSamples(kind);
    if (target <= 0) {
        return false;
    }
    auto it = m_captureBuffers.find(kind);
    if (it == m_captureBuffers.end()) {
        return false;
    }
    const auto &buffers = it.value();
    for (int ch : channels) {
        if (ch < 0 || ch >= buffers.size()) {
            return false;
        }
        if (buffers[ch].size() < target) {
            return false;
        }
    }
    return true;
}

QVector<double> DataCaptureCard::downsampleWindow(const QVector<double> &buffer, int windowCount, int targetPoints) const
{
    if (windowCount <= 0 || buffer.isEmpty() || targetPoints <= 0) {
        return {};
    }
    const int start = qMax(0, buffer.size() - windowCount);
    const int count = buffer.size() - start;
    if (count <= targetPoints) {
        return buffer.mid(start, count);
    }

    QVector<double> out;
    out.reserve(targetPoints);
    const double step = static_cast<double>(count) / static_cast<double>(targetPoints);
    for (int i = 0; i < targetPoints; ++i) {
        const int idx = start + static_cast<int>(i * step);
        out.push_back(buffer[qBound(start, idx, buffer.size() - 1)]);
    }
    return out;
}

void DataCaptureCard::flushAlignedWindowToPlot()
{
    const QSet<int> sel5322 = selectedChannels(JYDeviceKind::PXIe5322);
    const QSet<int> sel5323 = selectedChannels(JYDeviceKind::PXIe5323);
    const QSet<int> sel8902 = selectedChannels(JYDeviceKind::PXIe8902);

    if (m_plot) {
        m_plot->setSampleIntervalMicroseconds(plotIntervalUsForSelection(sel5322, sel5323, sel8902));
    }

    if (!hasWindowReady(sel5322, JYDeviceKind::PXIe5322)
        || !hasWindowReady(sel5323, JYDeviceKind::PXIe5323)
        || !hasWindowReady(sel8902, JYDeviceKind::PXIe8902)) {
        return;
    }

    if (m_waveformManager) {
        QSet<QString> activeSeries;
        QStringList seriesKeys;
        seriesKeys.reserve(m_channels.size());
        for (const auto &slot : m_channels) {
            if (slot.graph && slot.check && slot.check->isChecked()) {
                const QString key = WaveformDataManager::seriesKey(slot.kind, slot.channel);
                activeSeries.insert(key);
                seriesKeys.push_back(key);
            }
        }

        m_waveformManager->setActiveSeries(activeSeries);
        if (seriesKeys.isEmpty()) {
            return;
        }

        const int pixelWidth = m_plot ? qMax(64, m_plot->plotAreaWidth()) : m_displayMaxPoints;
        m_waveformManager->setStoragePointBudget(qMax(1000, pixelWidth * 6));
        const bool autoFollow = m_plot ? !m_plot->isUserOverrideActive() : true;
        const double xLower = (m_plot && !autoFollow) ? qMax(0.0, m_plot->visibleLowerBound()) : 0.0;
        const double xUpper = (m_plot && !autoFollow)
            ? qMin(m_displayRetentionSeconds, m_plot->visibleUpperBound())
            : m_displayRetentionSeconds;
        m_waveformManager->requestView(seriesKeys,
                                       xLower,
                                       xUpper,
                                       m_displayRetentionSeconds,
                                       pixelWidth,
                                       autoFollow);
        return;
    }

    const int pointsPerSecond = qMax(1, static_cast<int>(m_displayMaxPoints / m_displayWindowSeconds));
    const int windowPoints = qMax(1, static_cast<int>(m_windowSeconds * pointsPerSecond));
    const bool useFull = true;

    for (auto &slot : m_channels) {
        if (!slot.graph || !slot.check || !slot.check->isChecked()) {
            continue;
        }
        const int target = windowSamples(slot.kind);
        if (target <= 0) {
            continue;
        }
        const auto it = m_captureBuffers.find(slot.kind);
        if (it == m_captureBuffers.end()) {
            continue;
        }
        const auto &buffers = it.value();
        if (slot.channel < 0 || slot.channel >= buffers.size()) {
            continue;
        }
        auto &displayByKind = m_displayBuffers[slot.kind];
        if (displayByKind.size() != buffers.size()) {
            displayByKind.clear();
            displayByKind.resize(buffers.size());
        }

        const int targetPoints = useFull ? target : windowPoints;
        const QVector<double> windowSlice = downsampleWindow(buffers[slot.channel], target, targetPoints);
        auto &displayBuf = displayByKind[slot.channel];
        displayBuf.reserve(displayBuf.size() + windowSlice.size());
        for (double v : windowSlice) {
            displayBuf.push_back(v);
        }

        if (useFull) {
            const int keep = retentionSamples(slot.kind);
            if (keep > 0 && displayBuf.size() > keep) {
                displayBuf.remove(0, displayBuf.size() - keep);
            }
        } else {
            if (displayBuf.size() > m_displayMaxPoints) {
                displayBuf.remove(0, displayBuf.size() - m_displayMaxPoints);
            }
        }

        slot.buffer = displayBuf;
    }

    m_activeGraphs.clear();
    m_activeBuffers.clear();
    QVector<qint64> intervals;
    for (const auto &slot : m_channels) {
        if (slot.graph) {
            m_activeGraphs.push_back(slot.graph);
            m_activeBuffers.push_back(slot.buffer);
            qint64 intervalUs = 50000;
            if (slot.kind == JYDeviceKind::PXIe5322) {
                intervalUs = 1;
            } else if (slot.kind == JYDeviceKind::PXIe5323) {
                intervalUs = 5;
            } else if (slot.kind == JYDeviceKind::PXIe8902) {
                intervalUs = 16667;
            }
            intervals.push_back(intervalUs);
        }
    }
    if (m_plot) {
        m_plot->updateRealTimeLines(m_activeGraphs, m_activeBuffers, intervals);
    }

    for (auto kind : {JYDeviceKind::PXIe5322, JYDeviceKind::PXIe5323, JYDeviceKind::PXIe8902}) {
        if (!m_captureBuffers.contains(kind)) {
            continue;
        }
        auto &buffers = m_captureBuffers[kind];
        for (auto &buf : buffers) {
            buf.clear();
        }
    }
}

QString DataCaptureCard::kindLabel(JYDeviceKind kind) const
{
    switch (kind) {
        case JYDeviceKind::PXIe5322:
            return QStringLiteral("5322");
        case JYDeviceKind::PXIe5323:
            return QStringLiteral("5323");
        case JYDeviceKind::PXIe8902:
            return QStringLiteral("8902");
        default:
            return QStringLiteral("Unknown");
    }
}

void DataCaptureCard::rebuildActiveGraphs()
{
    if (!m_plot) {
        return;
    }

    for (auto &slot : m_channels) {
        const bool want = slot.check && slot.check->isChecked();
        if (want && !slot.graph) {
            QString deviceName;
            if (slot.kind == JYDeviceKind::PXIe5322) {
                deviceName = QStringLiteral("5322");
            } else if (slot.kind == JYDeviceKind::PXIe5323) {
                deviceName = QStringLiteral("5323");
            } else {
                deviceName = QStringLiteral("8902");
            }
            const QString name = QStringLiteral("%1-%2").arg(deviceName, slot.name);
            slot.graph = m_plot->addRealTimeLine(name);
            slot.buffer.clear();
        } else if (!want && slot.graph) {
            m_plot->removeGraph(slot.graph);
            slot.graph = nullptr;
            slot.buffer.clear();
        }
    }

    m_activeGraphs.clear();
    m_activeBuffers.clear();
    QSet<QString> activeSeries;
    for (const auto &slot : m_channels) {
        if (slot.graph) {
            m_activeGraphs.push_back(slot.graph);
            m_activeBuffers.push_back(slot.buffer);
            activeSeries.insert(WaveformDataManager::seriesKey(slot.kind, slot.channel));
        }
    }
    if (m_waveformManager) {
        m_waveformManager->setActiveSeries(activeSeries);
    }
}

void DataCaptureCard::updateChannelStatus(ChannelSlot &slot, bool checked)
{
    if (slot.status) {
        slot.status->setText(checked ? QStringLiteral("已订阅") : QStringLiteral("未订阅"));
    }
}

void DataCaptureCard::updateChannelSummary(ChannelSlot &slot)
{
    if (!slot.summary) {
        return;
    }

    if (slot.kind != JYDeviceKind::PXIe8902) {
        slot.summary->setText(QStringLiteral("采集通道"));
        return;
    }

    auto functionText = [](int func) {
        switch (func) {
            case JY8902_DMM_MeasurementFunction::JY8902_DC_Volts:
                return QStringLiteral("直流电压");
            case JY8902_DMM_MeasurementFunction::JY8902_AC_Volts:
                return QStringLiteral("交流电压");
            case JY8902_DMM_MeasurementFunction::JY8902_DC_Current:
                return QStringLiteral("直流电流");
            case JY8902_DMM_MeasurementFunction::JY8902_AC_Current:
                return QStringLiteral("交流电流");
            case JY8902_DMM_MeasurementFunction::JY8902_2_Wire_Resistance:
                return QStringLiteral("二线电阻");
            case JY8902_DMM_MeasurementFunction::JY8902_4_Wire_Resistance:
                return QStringLiteral("四线电阻");
            default:
                return QStringLiteral("未知");
        }
    };

    auto rangeText = [](int range) {
        if (range < 0) {
            return QStringLiteral("自动量程");
        }
        switch (range) {
            case JY8902_DMM_DC_VoltRange::JY8902_DC_Volt_200mV:
                return QStringLiteral("200mV");
            case JY8902_DMM_DC_VoltRange::JY8902_DC_Volt_2V:
                return QStringLiteral("2V");
            case JY8902_DMM_DC_VoltRange::JY8902_DC_Volt_20V:
                return QStringLiteral("20V");
            case JY8902_DMM_DC_VoltRange::JY8902_DC_Volt_240V:
                return QStringLiteral("240V");
            case JY8902_DMM_AC_VoltRange::JY8902_AC_Volt_200mV:
                return QStringLiteral("200mV");
            case JY8902_DMM_AC_VoltRange::JY8902_AC_Volt_2V:
                return QStringLiteral("2V");
            case JY8902_DMM_AC_VoltRange::JY8902_AC_Volt_20V:
                return QStringLiteral("20V");
            case JY8902_DMM_AC_VoltRange::JY8902_AC_Volt_240V:
                return QStringLiteral("240V");
            case JY8902_DMM_DC_CurrentRange::JY8902_DC_Current_20mA:
                return QStringLiteral("20mA");
            case JY8902_DMM_DC_CurrentRange::JY8902_DC_Current_200mA:
                return QStringLiteral("200mA");
            case JY8902_DMM_DC_CurrentRange::JY8902_DC_Current_1000mA:
                return QStringLiteral("1000mA");
            case JY8902_DMM_AC_CurrentRange::JY8902_AC_Current_20mA:
                return QStringLiteral("20mA");
            case JY8902_DMM_AC_CurrentRange::JY8902_AC_Current_200mA:
                return QStringLiteral("200mA");
            case JY8902_DMM_AC_CurrentRange::JY8902_AC_Current_1000mA:
                return QStringLiteral("1000mA");
            case JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_100:
                return QStringLiteral("100Ω");
            case JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_1K:
                return QStringLiteral("1kΩ");
            case JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_10K:
                return QStringLiteral("10kΩ");
            case JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_100K:
                return QStringLiteral("100kΩ");
            case JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_1M:
                return QStringLiteral("1MΩ");
            case JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_10M:
                return QStringLiteral("10MΩ");
            case JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_100M:
                return QStringLiteral("100MΩ");
            case JY8902_DMM_4_Wire_ResistanceRange::JY8902_4_Wire_Resistance_100:
                return QStringLiteral("100Ω");
            case JY8902_DMM_4_Wire_ResistanceRange::JY8902_4_Wire_Resistance_1K:
                return QStringLiteral("1kΩ");
            case JY8902_DMM_4_Wire_ResistanceRange::JY8902_4_Wire_Resistance_10K:
                return QStringLiteral("10kΩ");
            case JY8902_DMM_4_Wire_ResistanceRange::JY8902_4_Wire_Resistance_100K:
                return QStringLiteral("100kΩ");
            case JY8902_DMM_4_Wire_ResistanceRange::JY8902_4_Wire_Resistance_1M:
                return QStringLiteral("1MΩ");
            default:
                return QStringLiteral("未知量程");
        }
    };

    slot.summary->setText(QStringLiteral("%1 | %2")
                              .arg(functionText(m_8902Config.measurementFunction))
                              .arg(rangeText(m_8902Config.range)));
}

void DataCaptureCard::open8902ConfigDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("配置 JY8902"));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();

    auto *typeBox = new QComboBox(&dialog);
    typeBox->addItem(QStringLiteral("直流电压"), JY8902_DMM_MeasurementFunction::JY8902_DC_Volts);
    typeBox->addItem(QStringLiteral("交流电压"), JY8902_DMM_MeasurementFunction::JY8902_AC_Volts);
    typeBox->addItem(QStringLiteral("直流电流"), JY8902_DMM_MeasurementFunction::JY8902_DC_Current);
    typeBox->addItem(QStringLiteral("交流电流"), JY8902_DMM_MeasurementFunction::JY8902_AC_Current);
    typeBox->addItem(QStringLiteral("二线电阻"), JY8902_DMM_MeasurementFunction::JY8902_2_Wire_Resistance);
    typeBox->addItem(QStringLiteral("四线电阻"), JY8902_DMM_MeasurementFunction::JY8902_4_Wire_Resistance);
    const int idxType = typeBox->findData(m_8902Config.measurementFunction);
    if (idxType >= 0) {
        typeBox->setCurrentIndex(idxType);
    }

    auto *rangeBox = new QComboBox(&dialog);
    auto rebuildRanges = [rangeBox](int func) {
        rangeBox->clear();
        rangeBox->addItem(QStringLiteral("自动量程"), -1);
        if (func == JY8902_DMM_MeasurementFunction::JY8902_DC_Volts) {
            rangeBox->addItem(QStringLiteral("200mV"), JY8902_DMM_DC_VoltRange::JY8902_DC_Volt_200mV);
            rangeBox->addItem(QStringLiteral("2V"), JY8902_DMM_DC_VoltRange::JY8902_DC_Volt_2V);
            rangeBox->addItem(QStringLiteral("20V"), JY8902_DMM_DC_VoltRange::JY8902_DC_Volt_20V);
            rangeBox->addItem(QStringLiteral("240V"), JY8902_DMM_DC_VoltRange::JY8902_DC_Volt_240V);
        } else if (func == JY8902_DMM_MeasurementFunction::JY8902_AC_Volts) {
            rangeBox->addItem(QStringLiteral("200mV"), JY8902_DMM_AC_VoltRange::JY8902_AC_Volt_200mV);
            rangeBox->addItem(QStringLiteral("2V"), JY8902_DMM_AC_VoltRange::JY8902_AC_Volt_2V);
            rangeBox->addItem(QStringLiteral("20V"), JY8902_DMM_AC_VoltRange::JY8902_AC_Volt_20V);
            rangeBox->addItem(QStringLiteral("240V"), JY8902_DMM_AC_VoltRange::JY8902_AC_Volt_240V);
        } else if (func == JY8902_DMM_MeasurementFunction::JY8902_DC_Current) {
            rangeBox->addItem(QStringLiteral("20mA"), JY8902_DMM_DC_CurrentRange::JY8902_DC_Current_20mA);
            rangeBox->addItem(QStringLiteral("200mA"), JY8902_DMM_DC_CurrentRange::JY8902_DC_Current_200mA);
            rangeBox->addItem(QStringLiteral("1000mA"), JY8902_DMM_DC_CurrentRange::JY8902_DC_Current_1000mA);
        } else if (func == JY8902_DMM_MeasurementFunction::JY8902_AC_Current) {
            rangeBox->addItem(QStringLiteral("20mA"), JY8902_DMM_AC_CurrentRange::JY8902_AC_Current_20mA);
            rangeBox->addItem(QStringLiteral("200mA"), JY8902_DMM_AC_CurrentRange::JY8902_AC_Current_200mA);
            rangeBox->addItem(QStringLiteral("1000mA"), JY8902_DMM_AC_CurrentRange::JY8902_AC_Current_1000mA);
        } else if (func == JY8902_DMM_MeasurementFunction::JY8902_2_Wire_Resistance) {
            rangeBox->addItem(QStringLiteral("100Ω"), JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_100);
            rangeBox->addItem(QStringLiteral("1kΩ"), JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_1K);
            rangeBox->addItem(QStringLiteral("10kΩ"), JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_10K);
            rangeBox->addItem(QStringLiteral("100kΩ"), JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_100K);
            rangeBox->addItem(QStringLiteral("1MΩ"), JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_1M);
            rangeBox->addItem(QStringLiteral("10MΩ"), JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_10M);
            rangeBox->addItem(QStringLiteral("100MΩ"), JY8902_DMM_2_Wire_ResistanceRange::JY8902_2_Wire_Resistance_100M);
        } else if (func == JY8902_DMM_MeasurementFunction::JY8902_4_Wire_Resistance) {
            rangeBox->addItem(QStringLiteral("100Ω"), JY8902_DMM_4_Wire_ResistanceRange::JY8902_4_Wire_Resistance_100);
            rangeBox->addItem(QStringLiteral("1kΩ"), JY8902_DMM_4_Wire_ResistanceRange::JY8902_4_Wire_Resistance_1K);
            rangeBox->addItem(QStringLiteral("10kΩ"), JY8902_DMM_4_Wire_ResistanceRange::JY8902_4_Wire_Resistance_10K);
            rangeBox->addItem(QStringLiteral("100kΩ"), JY8902_DMM_4_Wire_ResistanceRange::JY8902_4_Wire_Resistance_100K);
            rangeBox->addItem(QStringLiteral("1MΩ"), JY8902_DMM_4_Wire_ResistanceRange::JY8902_4_Wire_Resistance_1M);
        }
    };

    rebuildRanges(typeBox->currentData().toInt());
    const int idxRange = rangeBox->findData(m_8902Config.range);
    if (idxRange >= 0) {
        rangeBox->setCurrentIndex(idxRange);
    }

    connect(typeBox, &QComboBox::currentIndexChanged, &dialog, [typeBox, rangeBox, rebuildRanges](int) {
        rebuildRanges(typeBox->currentData().toInt());
        rangeBox->setCurrentIndex(0);
    });

    form->addRow(QStringLiteral("测量类型"), typeBox);
    form->addRow(QStringLiteral("量程"), rangeBox);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        m_8902Config.measurementFunction = typeBox->currentData().toInt();
        m_8902Config.range = rangeBox->currentData().toInt();
        for (auto &slot : m_channels) {
            if (slot.kind == JYDeviceKind::PXIe8902) {
                updateChannelSummary(slot);
            }
        }
        emit config8902Changed(m_8902Config);
    }
}

void DataCaptureCard::appendSample(JYDeviceKind kind, int channel, double value)
{
    for (auto &slot : m_channels) {
        if (slot.kind == kind && slot.channel == channel && slot.graph) {
            slot.buffer.push_back(value);
            return;
        }
    }
}

QSet<int> DataCaptureCard::selectedChannels(JYDeviceKind kind) const
{
    QSet<int> out;
    for (const auto &slot : m_channels) {
        if (slot.kind == kind && slot.check && slot.check->isChecked()) {
            out.insert(slot.channel);
        }
    }
    return out;
}
