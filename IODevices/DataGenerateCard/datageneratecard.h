#ifndef DATAGENERATECARD_H
#define DATAGENERATECARD_H

#include <QMap>
#include <QString>
#include <QVector>
#include <QWidget>

#include "../JYDevices/5711waveformconfig.h"
#include "../JYDevices/jydevicetype.h"

class JYDeviceWorker;
class JYThreadManager;
class QCheckBox;
class QEvent;
class QFrame;
class QGridLayout;
class QLabel;
class QPushButton;

namespace Ui {
class DataGenerateCard;
}

class DataGenerateCard : public QWidget
{
    Q_OBJECT

public:
    explicit DataGenerateCard(QWidget *parent = nullptr);
    ~DataGenerateCard();

    void setJYThreadManager(JYThreadManager *manager) { m_manager = manager; }

signals:
    void startOutputRequested(const QVector<int> &channels);
    void stopOutputRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct ChannelConfig {
        enum class Mode { Current, Voltage } mode = Mode::Current;
        JY5711WaveformConfig waveform;
    };

    struct ChannelWidgets {
        QFrame *frame = nullptr;
        QCheckBox *select = nullptr;
        QLabel *title = nullptr;
        QLabel *summary = nullptr;
        QLabel *status = nullptr;
    };

    void buildUi();
    void applyThemeQss();
    void buildChannels();
    ChannelWidgets createChannelCard(int channel);
    void updateChannelSummary(int channel);
    void updateChannelStatus(int channel, const QString &text);
    void openChannelEditor(int channel);
    QVector<PXIe5711_testtype> waveformOptions() const;
    JYDeviceConfig build5711Config(const QVector<int> &channels) const;
    void ensure5711Worker();

    QGridLayout *m_channelsLayout = nullptr;
    QPushButton *m_btnStart = nullptr;
    QPushButton *m_btnStop = nullptr;
    QLabel *m_labelSelection = nullptr;

    QMap<int, ChannelConfig> m_channelConfigs;
    QMap<int, ChannelWidgets> m_channelWidgets;

    QString m_loadedTheme;
    bool m_applyingQss = false;

    JYThreadManager *m_manager = nullptr;
    JYDeviceWorker *m_worker5711 = nullptr;

    Ui::DataGenerateCard *ui;
};

#endif // DATAGENERATECARD_H
