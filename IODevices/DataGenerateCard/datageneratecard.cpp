#include "datageneratecard.h"
#include "ui_datageneratecard.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QFile>

#include "../JYDevices/jythreadmanager.h"
#include "../JYDevices/jydeviceworker.h"

namespace {
QString channelTitle(int channel)
{
    return QStringLiteral("CH%1").arg(channel, 2, 10, QChar('0'));
}

QString channelModeText(int channel)
{
    if (channel <= 15) {
        return QStringLiteral("电流输出");
    }
    return QStringLiteral("电压输出");
}
}

DataGenerateCard::DataGenerateCard(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DataGenerateCard)
{
    ui->setupUi(this);
    buildUi();
}

DataGenerateCard::~DataGenerateCard()
{
    delete ui;
}

bool DataGenerateCard::eventFilter(QObject *watched, QEvent *event)
{
    if (!event || event->type() != QEvent::MouseButtonDblClick) {
        return QWidget::eventFilter(watched, event);
    }

    for (auto it = m_channelWidgets.begin(); it != m_channelWidgets.end(); ++it) {
        if (it.value().frame == watched) {
            openChannelEditor(it.key());
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void DataGenerateCard::buildUi()
{
    m_channelsLayout = ui->channelsLayout;
    m_btnStart = ui->btnStartorStopOutput;
    m_btnStop = nullptr;
    m_labelSelection = ui->labelSelectionHint;

    applyThemeQss();
    buildChannels();

    if (m_btnStart) {
        connect(m_btnStart, &QPushButton::clicked, this, [this]() {
            const bool isStopping = (m_btnStart->text() == QStringLiteral("停止输出"));
            if (isStopping) {
                for (auto it = m_channelWidgets.begin(); it != m_channelWidgets.end(); ++it) {
                    updateChannelStatus(it.key(), QStringLiteral("已停止"));
                }
                if (m_worker5711) {
                    m_worker5711->postStop();
                }
                m_btnStart->setText(QStringLiteral("开始输出"));
                emit stopOutputRequested();
                return;
            }

            QVector<int> selected;
            for (auto it = m_channelWidgets.begin(); it != m_channelWidgets.end(); ++it) {
                if (it.value().select && it.value().select->isChecked()) {
                    selected.push_back(it.key());
                }
            }

            if (selected.isEmpty()) {
                QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择需要输出的通道"));
                return;
            }

            if (m_manager && !m_manager->isDeviceInitialized(JYDeviceKind::PXIe5711)) {
                QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("JY5711 未初始化，无法开始输出"));
                return;
            }

            for (int ch : selected) {
                updateChannelStatus(ch, QStringLiteral("输出中"));
            }

            if (m_manager) {
                ensure5711Worker();
                if (m_worker5711) {
                    const JYDeviceConfig cfg = build5711Config(selected);
                    m_worker5711->postConfigure(cfg);
                    m_worker5711->postStart();
                    m_worker5711->postTrigger();
                }
            }

            m_btnStart->setText(QStringLiteral("停止输出"));
            emit startOutputRequested(selected);
        });
    }
}

void DataGenerateCard::ensure5711Worker()
{
    if (m_worker5711 || !m_manager) {
        return;
    }
    m_worker5711 = m_manager->create5711Worker();
}

JYDeviceConfig DataGenerateCard::build5711Config(const QVector<int> &channels) const
{
    JYDeviceConfig config;
    config.kind = JYDeviceKind::PXIe5711;

    JY5711Config cfg5711;
    cfg5711.channelCount = 0;
    cfg5711.sampleRate = 1000000.0;
    cfg5711.lowRange = -10.0;
    cfg5711.highRange = 10.0;

    int maxChannel = -1;
    for (int ch : channels) {
        cfg5711.enabledChannels.push_back(ch);
        if (ch > maxChannel) {
            maxChannel = ch;
        }
    }
    cfg5711.channelCount = maxChannel + 1;
    if (cfg5711.channelCount <= 0) {
        cfg5711.channelCount = 1;
    }

    for (int ch : channels) {
        const auto cfgIt = m_channelConfigs.find(ch);
        if (cfgIt == m_channelConfigs.end()) {
            continue;
        }
        const ChannelConfig &src = cfgIt.value();
        JY5711WaveformConfig wf;
        wf.channel = ch;
        if (src.mode == ChannelConfig::Mode::Current) {
            wf.type = PXIe5711_testtype::HighLevelWave;
            wf.amplitude = src.currentMa;
            wf.frequency = 0.0;
            wf.dutyCycle = 1.0;
        } else {
            wf.type = src.waveform;
            wf.amplitude = src.amplitude;
            wf.frequency = src.frequency;
            wf.dutyCycle = src.duty;
        }
        cfg5711.waveforms.push_back(wf);
    }

    config.cfg5711 = cfg5711;
    return config;
}

void DataGenerateCard::applyThemeQss()
{
    const QString theme = qApp ? qApp->property("atsTheme").toString().toLower() : QString();
    if (m_applyingQss) {
        return;
    }
    if (!theme.isEmpty() && theme == m_loadedTheme) {
        return;
    }
    const QString qssPath = (theme == QStringLiteral("light"))
        ? QStringLiteral(":/styles/datageneratecard_light.qss")
        : QStringLiteral(":/styles/datageneratecard_dark.qss");
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

void DataGenerateCard::changeEvent(QEvent *event)
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

void DataGenerateCard::buildChannels()
{
    if (!m_channelsLayout) {
        return;
    }

    const int totalChannels = 32;
    const int columns = 4;

    for (int ch = 0; ch < totalChannels; ++ch) {
        ChannelConfig cfg;
        cfg.mode = (ch <= 15) ? ChannelConfig::Mode::Current : ChannelConfig::Mode::Voltage;
        m_channelConfigs.insert(ch, cfg);

        ChannelWidgets widgets = createChannelCard(ch);
        m_channelWidgets.insert(ch, widgets);

        const int row = ch / columns;
        const int col = ch % columns;
        m_channelsLayout->addWidget(widgets.frame, row, col);
        updateChannelSummary(ch);
        updateChannelStatus(ch, QStringLiteral("未输出"));
    }
}

DataGenerateCard::ChannelWidgets DataGenerateCard::createChannelCard(int channel)
{
    ChannelWidgets widgets;
    QWidget *parentWidget = this;
    if (ui && ui->scrollAreaWidgetContents) {
        parentWidget = ui->scrollAreaWidgetContents;
    }
    widgets.frame = new QFrame(parentWidget);
    widgets.frame->setObjectName(QStringLiteral("channelCard_%1").arg(channel));
    widgets.frame->setProperty("channelCard", true);
    widgets.frame->setFrameShape(QFrame::StyledPanel);
    widgets.frame->setFrameShadow(QFrame::Raised);
    widgets.frame->setMinimumSize(QSize(160, 80));
    widgets.frame->installEventFilter(this);

    auto *layout = new QVBoxLayout(widgets.frame);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);

    auto *topRow = new QHBoxLayout();
    widgets.title = new QLabel(QStringLiteral("%1").arg(channelTitle(channel)), widgets.frame);
    widgets.title->setStyleSheet(QStringLiteral("font-weight:600;"));
    widgets.select = new QCheckBox(QStringLiteral("选择"), widgets.frame);
    topRow->addWidget(widgets.title);
    topRow->addStretch();
    topRow->addWidget(widgets.select);

    widgets.summary = new QLabel(QStringLiteral("%1").arg(channelModeText(channel)), widgets.frame);
    widgets.summary->setObjectName(QStringLiteral("labelSummary"));
    widgets.status = new QLabel(QStringLiteral("未输出"), widgets.frame);
    widgets.status->setObjectName(QStringLiteral("labelStatus"));

    layout->addLayout(topRow);
    layout->addWidget(widgets.summary);
    layout->addWidget(widgets.status);

    return widgets;
}

void DataGenerateCard::updateChannelSummary(int channel)
{
    if (!m_channelWidgets.contains(channel) || !m_channelConfigs.contains(channel)) {
        return;
    }

    const ChannelConfig &cfg = m_channelConfigs[channel];
    ChannelWidgets &widgets = m_channelWidgets[channel];

    if (!widgets.summary) {
        return;
    }

    if (cfg.mode == ChannelConfig::Mode::Current) {
        widgets.summary->setText(QStringLiteral("电流: %1 mA").arg(cfg.currentMa, 0, 'f', 2));
    } else {
        widgets.summary->setText(QStringLiteral("%1 | 幅值%2 | 占空比%3 | 频率%4")
                                 .arg(PXIe5711_testtype_to_string(cfg.waveform))
                                 .arg(cfg.amplitude, 0, 'f', 2)
                                 .arg(cfg.duty, 0, 'f', 2)
                                 .arg(cfg.frequency, 0, 'f', 0));
    }
}

void DataGenerateCard::updateChannelStatus(int channel, const QString &text)
{
    if (!m_channelWidgets.contains(channel)) {
        return;
    }

    ChannelWidgets &widgets = m_channelWidgets[channel];
    if (widgets.status) {
        widgets.status->setText(text);
    }
}

void DataGenerateCard::openChannelEditor(int channel)
{
    if (!m_channelConfigs.contains(channel)) {
        return;
    }

    ChannelConfig cfg = m_channelConfigs[channel];
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("编辑 %1 输出参数").arg(channelTitle(channel)));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();

    if (cfg.mode == ChannelConfig::Mode::Current) {
        auto *currentSpin = new QDoubleSpinBox(&dialog);
        currentSpin->setRange(4.0, 20.0);
        currentSpin->setDecimals(2);
        currentSpin->setSuffix(QStringLiteral(" mA"));
        currentSpin->setValue(cfg.currentMa);
        form->addRow(QStringLiteral("电流大小"), currentSpin);

        layout->addLayout(form);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            cfg.currentMa = currentSpin->value();
            m_channelConfigs[channel] = cfg;
            updateChannelSummary(channel);
        }
        return;
    }

    auto *waveformBox = new QComboBox(&dialog);
    const auto options = waveformOptions();
    for (const auto &opt : options) {
        waveformBox->addItem(PXIe5711_testtype_to_string(opt), static_cast<int>(opt));
    }
    waveformBox->setCurrentText(PXIe5711_testtype_to_string(cfg.waveform));

    auto *amplitudeSpin = new QDoubleSpinBox(&dialog);
    amplitudeSpin->setRange(0.0, 10.0);
    amplitudeSpin->setDecimals(3);
    amplitudeSpin->setValue(cfg.amplitude);

    auto *dutySpin = new QDoubleSpinBox(&dialog);
    dutySpin->setRange(0.0, 1.0);
    dutySpin->setDecimals(3);
    dutySpin->setValue(cfg.duty);

    auto *freqSpin = new QDoubleSpinBox(&dialog);
    freqSpin->setRange(1.0, 100000.0);
    freqSpin->setDecimals(1);
    freqSpin->setValue(cfg.frequency);

    auto *offsetSpin = new QDoubleSpinBox(&dialog);
    offsetSpin->setRange(-20.0, 20.0);
    offsetSpin->setDecimals(3);
    offsetSpin->setValue(cfg.offset);

    form->addRow(QStringLiteral("波形类型"), waveformBox);
    form->addRow(QStringLiteral("幅值"), amplitudeSpin);
    form->addRow(QStringLiteral("占空比"), dutySpin);
    form->addRow(QStringLiteral("频率"), freqSpin);
    form->addRow(QStringLiteral("偏置"), offsetSpin);

    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        cfg.waveform = static_cast<PXIe5711_testtype>(waveformBox->currentData().toInt());
        cfg.amplitude = amplitudeSpin->value();
        cfg.duty = dutySpin->value();
        cfg.frequency = freqSpin->value();
        cfg.offset = offsetSpin->value();
        m_channelConfigs[channel] = cfg;
        updateChannelSummary(channel);
    }
}

QVector<PXIe5711_testtype> DataGenerateCard::waveformOptions() const
{
    return PXIe5711_waveform_options();
}