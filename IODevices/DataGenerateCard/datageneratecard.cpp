#include "datageneratecard.h"
#include "ui_datageneratecard.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include "../JYDevices/jydeviceworker.h"
#include "../JYDevices/jythreadmanager.h"

namespace {
QString channelTitle(int channel)
{
    return QStringLiteral("CH%1").arg(channel, 2, 10, QChar('0'));
}

QString channelTypeText(int channel)
{
    return (channel <= 15) ? QStringLiteral("电流输出") : QStringLiteral("电压输出");
}

QString stoppedStatusText()
{
    return QStringLiteral("未启动");
}

QString waveformSummaryText(const JY5711WaveformConfig &rawConfig)
{
    JY5711WaveformConfig config = rawConfig;
    config.ensureValid();

    QStringList parts;
    const auto specs = PXIe5711_waveform_param_specs(config.waveformId);
    for (const auto &spec : specs) {
        const double value = PXIe5711_param_value(config.params, spec.key, spec.defaultValue);
        parts.push_back(QStringLiteral("%1 %2%3")
                            .arg(spec.label)
                            .arg(value, 0, 'f', spec.decimals)
                            .arg(spec.suffix));
        if (parts.size() >= 4) {
            break;
        }
    }

    if (parts.isEmpty()) {
        return PXIe5711_waveform_display_name(config.waveformId);
    }
    return QStringLiteral("%1 | %2")
        .arg(PXIe5711_waveform_display_name(config.waveformId))
        .arg(parts.join(QStringLiteral(" | ")));
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

    if (!m_btnStart) {
        return;
    }

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
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请至少选择一个输出通道。"));
            return;
        }

        if (m_manager && !m_manager->isDeviceInitialized(JYDeviceKind::PXIe5711)) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("JY5711 设备尚未初始化。"));
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
        maxChannel = qMax(maxChannel, ch);
    }
    cfg5711.channelCount = qMax(1, maxChannel + 1);

    for (int ch : channels) {
        const auto cfgIt = m_channelConfigs.find(ch);
        if (cfgIt == m_channelConfigs.end()) {
            continue;
        }

        JY5711WaveformConfig wf = cfgIt.value().waveform;
        wf.channel = ch;
        wf.ensureValid();
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
        cfg.waveform.channel = ch;
        cfg.waveform.waveformId = PXIe5711_default_waveform_id();
        cfg.waveform.params = PXIe5711_default_param_map(cfg.waveform.waveformId);
        cfg.waveform.ensureValid();
        m_channelConfigs.insert(ch, cfg);

        ChannelWidgets widgets = createChannelCard(ch);
        m_channelWidgets.insert(ch, widgets);

        const int row = ch / columns;
        const int col = ch % columns;
        m_channelsLayout->addWidget(widgets.frame, row, col);
        updateChannelSummary(ch);
        updateChannelStatus(ch, stoppedStatusText());
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
    widgets.title = new QLabel(channelTitle(channel), widgets.frame);
    widgets.title->setStyleSheet(QStringLiteral("font-weight:600;"));
    widgets.select = new QCheckBox(QStringLiteral("选择"), widgets.frame);
    topRow->addWidget(widgets.title);
    topRow->addStretch();
    topRow->addWidget(widgets.select);

    widgets.summary = new QLabel(channelTypeText(channel), widgets.frame);
    widgets.summary->setObjectName(QStringLiteral("labelSummary"));
    widgets.status = new QLabel(stoppedStatusText(), widgets.frame);
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

    widgets.summary->setText(waveformSummaryText(cfg.waveform));
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
    cfg.waveform.ensureValid();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("编辑 %1").arg(channelTitle(channel)));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    auto *headForm = new QFormLayout();
    auto *waveformBox = new QComboBox(&dialog);
    const auto options = waveformOptions();
    for (const QString &waveformId : options) {
        waveformBox->addItem(PXIe5711_waveform_display_name(waveformId), waveformId);
    }
    waveformBox->setCurrentIndex(qMax(0, waveformBox->findData(cfg.waveform.waveformId)));
    headForm->addRow(QStringLiteral("波形类型"), waveformBox);
    layout->addLayout(headForm);

    auto *dynamicWidget = new QWidget(&dialog);
    auto *dynamicForm = new QFormLayout(dynamicWidget);
    dynamicForm->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(dynamicWidget);

    QMap<QString, QDoubleSpinBox *> editors;
    auto rebuildEditors = [&](const QString &waveformId, const QMap<QString, double> &values) {
        while (dynamicForm->rowCount() > 0) {
            dynamicForm->removeRow(0);
        }
        editors.clear();

        const auto specs = PXIe5711_waveform_param_specs(waveformId);
        const QMap<QString, double> mergedValues = PXIe5711_merge_params(waveformId, values);
        for (const auto &spec : specs) {
            auto *spin = new QDoubleSpinBox(dynamicWidget);
            spin->setRange(spec.minValue, spec.maxValue);
            spin->setDecimals(spec.decimals);
            spin->setSuffix(spec.suffix);
            spin->setValue(PXIe5711_param_value(mergedValues, spec.key, spec.defaultValue));
            dynamicForm->addRow(spec.label, spin);
            editors.insert(spec.key, spin);
        }

        dynamicWidget->adjustSize();
        dynamicWidget->updateGeometry();
        layout->activate();
        dialog.adjustSize();
        dialog.resize(dialog.sizeHint());
    };

    rebuildEditors(cfg.waveform.waveformId, cfg.waveform.params);
    connect(waveformBox, &QComboBox::currentIndexChanged, &dialog, [&]() {
        QMap<QString, double> currentValues;
        for (auto it = editors.cbegin(); it != editors.cend(); ++it) {
            currentValues.insert(it.key(), it.value()->value());
        }

        const QString waveformId = waveformBox->currentData().toString();
        rebuildEditors(waveformId, currentValues);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        cfg.waveform.waveformId = waveformBox->currentData().toString();
        cfg.waveform.params = PXIe5711_default_param_map(cfg.waveform.waveformId);
        for (auto it = editors.cbegin(); it != editors.cend(); ++it) {
            cfg.waveform.params.insert(it.key(), it.value()->value());
        }
        cfg.waveform.ensureValid();
        m_channelConfigs[channel] = cfg;
        updateChannelSummary(channel);
    }
}

QVector<QString> DataGenerateCard::waveformOptions() const
{
    return PXIe5711_waveform_ids();
}
