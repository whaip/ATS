#ifndef DATAGENERATECARD_H
#define DATAGENERATECARD_H

#include <QMap>
#include <QString>
#include <QVector>
#include <QWidget>

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
    // 每个输出通道的配置快照，当前主要记录输出模式和波形参数。
    struct ChannelConfig {
        enum class Mode { Current, Voltage } mode = Mode::Current;
        JY5711WaveformConfig waveform;
    };

    // 每个通道卡片上相关控件的集合，便于批量更新界面状态。
    struct ChannelWidgets {
        QFrame *frame = nullptr;
        QCheckBox *select = nullptr;
        QLabel *title = nullptr;
        QLabel *summary = nullptr;
        QLabel *status = nullptr;
    };

    // 构建页面布局和启动/停止按钮逻辑。
    void buildUi();
    // 根据主题切换对应的样式文件。
    void applyThemeQss();
    // 生成 32 个 5711 输出通道卡片。
    void buildChannels();
    ChannelWidgets createChannelCard(int channel);
    void updateChannelSummary(int channel);
    void updateChannelStatus(int channel, const QString &text);
    // 双击通道卡片后打开波形参数编辑器。
    void openChannelEditor(int channel);
    QVector<QString> waveformOptions() const;
    // 将当前勾选通道转换成 5711 设备配置，供 worker 下发。
    JYDeviceConfig build5711Config(const QVector<int> &channels) const;
    // 惰性创建 5711 worker，避免界面初始化时过早占用设备线程。
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
