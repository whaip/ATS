#ifndef JYDEVICEORCHESTRATOR_H
#define JYDEVICEORCHESTRATOR_H

#include "jydeviceworker.h"

#include <QObject>
#include <QVector>

class JYDeviceOrchestrator : public QObject
{
    Q_OBJECT
public:
    explicit JYDeviceOrchestrator(QObject *parent = nullptr);

    // 注册一个 worker，后续统一编排时会把它纳入同步启动/停止流程。
    void addWorker(JYDeviceWorker *worker);
    // 清空当前已注册的 worker 列表。
    void clearWorkers();

    // 按传入配置向所有启用设备广播 configure 请求。
    void configureAll(const JYDeviceConfig &config532x, const JYDeviceConfig &config5711, const JYDeviceConfig &config8902);
    // 向全部 worker 广播 start 请求。
    void startAll();
    // 向全部 worker 广播 trigger 请求。
    void triggerAll();
    // 向全部 worker 广播 stop 请求。
    void stopAll();
    // 向全部 worker 广播 close 请求。
    void closeAll();

    // 完整同步启动流程：configure -> 等待 Configured -> start -> 等待 Armed -> trigger -> 等待 Running。
    bool synchronizeStart(const JYDeviceConfig &config532x,
                          const JYDeviceConfig &config5711,
                          const JYDeviceConfig &config8902,
                          int timeoutMs,
                          qint64 *barrierReleaseMs = nullptr);
    // 针对上一次启动的设备集合执行同步停止。
    bool synchronizeStop(int timeoutMs);

signals:
    void syncFailed(const QString &reason);
    void syncSucceeded();

private:
    // 等待筛选后的所有 worker 进入目标状态；超时或出现 Faulted 即失败。
    bool waitForAll(JYDeviceState targetState,
                    int timeoutMs,
                    QString *reason,
                    const JYDeviceConfig &config532x,
                    const JYDeviceConfig &config5711,
                    const JYDeviceConfig &config8902,
                    bool useFilter);

    QVector<JYDeviceWorker *> m_workers;
    QMap<JYDeviceWorker *, JYDeviceState> m_states;
    bool m_hasLastConfig = false;
    JYDeviceConfig m_lastConfig532x;
    JYDeviceConfig m_lastConfig5711;
    JYDeviceConfig m_lastConfig8902;
};

#endif // JYDEVICEORCHESTRATOR_H
