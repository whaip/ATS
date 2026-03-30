#ifndef DATACAPTURECARD_H
#define DATACAPTURECARD_H

#include <QWidget>
#include <QVector>
#include <QSet>

#include "../JYDevices/jydevicetype.h"

class QHBoxLayout;
class QVBoxLayout;
class QScrollArea;
class QPushButton;
class QTimer;
class QFrame;
class QLabel;
class QCheckBox;
class QEvent;

class UESTCQCustomPlot;
class QCPGraph;
class JYThreadManager;
class WaveformDataManager;

namespace Ui {
class DataCaptureCard;
}

class DataCaptureCard : public QWidget
{
    Q_OBJECT

public:
    explicit DataCaptureCard(QWidget *parent = nullptr);
    ~DataCaptureCard();

    void setJYThreadManager(JYThreadManager *manager);

signals:
    void startRequested();
    void startRequestedWithChannels(const QSet<int> &channels5322, const QSet<int> &channels5323, const QSet<int> &channels8902);
    void stopRequested();
    void config8902Changed(const JY8902Config &config);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct ChannelSlot {
        JYDeviceKind kind = JYDeviceKind::PXIe5322;
        int channel = 0;
        QString name;
        QFrame *frame = nullptr;
        QCheckBox *check = nullptr;
        QLabel *summary = nullptr;
        QLabel *status = nullptr;
        QCPGraph *graph = nullptr;
        QVector<double> buffer;
    };

    void buildLayout();
    void applyThemeQss();
    void initPlotLines();
    QFrame *createChannelCard(JYDeviceKind kind, int channel, const QColor &color);
    void setRunning(bool running);
    void onAlignedBatch(const JYAlignedBatch &batch);
    int deviceIndex(JYDeviceKind kind) const;
    void rebuildActiveGraphs();
    void updateChannelStatus(ChannelSlot &slot, bool checked);
    void updateChannelSummary(ChannelSlot &slot);
    void open8902ConfigDialog();
    void appendSample(JYDeviceKind kind, int channel, double value);
    QSet<int> selectedChannels(JYDeviceKind kind) const;
    QString kindLabel(JYDeviceKind kind) const;
    int windowSamples(JYDeviceKind kind) const;
    int retentionSamples(JYDeviceKind kind) const;
    qint64 plotIntervalUsForSelection(const QSet<int> &channels5322,
                                      const QSet<int> &channels5323,
                                      const QSet<int> &channels8902) const;
    void appendPacketToBuffers(JYDeviceKind kind, const JYDataPacket &packet);
    bool hasWindowReady(const QSet<int> &channels, JYDeviceKind kind) const;
    QVector<double> downsampleWindow(const QVector<double> &buffer, int windowCount, int targetPoints) const;
    void flushAlignedWindowToPlot();

    Ui::DataCaptureCard *ui;

    JYThreadManager *m_manager = nullptr;

    UESTCQCustomPlot *m_plot = nullptr;
    QScrollArea *m_portScroll = nullptr;
    QWidget *m_portContainer = nullptr;
    QVBoxLayout *m_portLayout = nullptr;
    QPushButton *m_btnStartStop = nullptr;
    QLabel *m_labelCaptureStatus = nullptr;
    QTimer *m_simTimer = nullptr;
    bool m_running = false;
    bool m_useTestData = true;
    QString m_loadedTheme;
    bool m_applyingQss = false;
    JY8902Config m_8902Config;

    QVector<QCPGraph *> m_graphs;
    QVector<QVector<double>> m_buffers;
    int m_sampleIndex = 0;
    QVector<QLabel *> m_portStatusLabels;
    QVector<ChannelSlot> m_channels;
    QVector<QCPGraph *> m_activeGraphs;
    QVector<QVector<double>> m_activeBuffers;
    int m_batchCount = 0;
    qint64 m_lastBatchTs = 0;
    WaveformDataManager *m_waveformManager = nullptr;
    QTimer *m_viewRefreshTimer = nullptr;
    bool m_viewRefreshPending = false;
    bool m_updatingPlotView = false;
    double m_lastFrameLatestX = 0.0;
    QMap<JYDeviceKind, QVector<QVector<double>>> m_captureBuffers;
    QMap<JYDeviceKind, QVector<QVector<double>>> m_displayBuffers;
    double m_windowSeconds = 3.0;
    double m_displayWindowSeconds = 3.0;
    double m_displayRetentionSeconds = 3.0;
    int m_plotMaxPoints = 600;
    int m_displayMaxPoints = 600;
};

#endif // DATACAPTURECARD_H
