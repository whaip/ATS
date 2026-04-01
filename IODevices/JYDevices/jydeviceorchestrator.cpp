#include "jydeviceorchestrator.h"
#include "jydeviceconfigutils.h"

#include <QDateTime>
#include <QEventLoop>
#include <QTimer>

namespace {
bool isEnabled(JYDeviceKind kind,
               const JYDeviceConfig &config532x,
               const JYDeviceConfig &config5711,
               const JYDeviceConfig &config8902)
{
    switch (kind) {
        case JYDeviceKind::PXIe5322:
        case JYDeviceKind::PXIe5323:
            return config532x.cfg532x.channelCount > 0 && config532x.cfg532x.slotNumber >= 0;
        case JYDeviceKind::PXIe5711:
            return config5711.cfg5711.channelCount > 0 && config5711.cfg5711.slotNumber >= 0;
        case JYDeviceKind::PXIe8902:
            return config8902.cfg8902.sampleCount > 0 && config8902.cfg8902.slotNumber >= 0;
    }
    return false;
}
}

JYDeviceOrchestrator::JYDeviceOrchestrator(QObject *parent)
    : QObject(parent)
{
}

void JYDeviceOrchestrator::addWorker(JYDeviceWorker *worker)
{
    if (worker && !m_workers.contains(worker)) {
        m_workers.push_back(worker);
        m_states[worker] = worker->state();
        connect(worker, &JYDeviceWorker::statusChanged, this,
                [this, worker](JYDeviceKind, JYDeviceState state, const QString &) {
                    m_states[worker] = state;
                });
    }
}

void JYDeviceOrchestrator::clearWorkers()
{
    m_workers.clear();
    m_states.clear();
}

void JYDeviceOrchestrator::configureAll(const JYDeviceConfig &config532x, const JYDeviceConfig &config5711, const JYDeviceConfig &config8902)
{
    for (auto *worker : m_workers) {
        if (!worker) continue;
        if (!isEnabled(worker->kind(), config532x, config5711, config8902)) {
            continue;
        }
        switch (worker->kind()) {
            case JYDeviceKind::PXIe5322:
            case JYDeviceKind::PXIe5323:
            {
                const JYDeviceKind kind = worker->kind();
                JYDeviceConfig cfg = build532xInitConfig(kind);
                const int maxChannels = (kind == JYDeviceKind::PXIe5322) ? 16 : 32;
                cfg.cfg532x.channelCount = qMin(config532x.cfg532x.channelCount, maxChannels);
                cfg.cfg532x.samplesPerRead = config532x.cfg532x.samplesPerRead;
                cfg.cfg532x.timeoutMs = config532x.cfg532x.timeoutMs;
                cfg.cfg532x.lowRange = config532x.cfg532x.lowRange;
                cfg.cfg532x.highRange = config532x.cfg532x.highRange;
                cfg.cfg532x.bandwidth = config532x.cfg532x.bandwidth;
                cfg.cfg532x.sampleRate = (kind == JYDeviceKind::PXIe5322) ? 1000000.0 : 200000.0;
                worker->postConfigure(cfg);
                break;
            }
            case JYDeviceKind::PXIe5711:
                worker->postConfigure(config5711);
                break;
            case JYDeviceKind::PXIe8902:
                worker->postConfigure(config8902);
                break;
        }
    }
}

void JYDeviceOrchestrator::startAll()
{
    for (auto *worker : m_workers) {
        if (worker) {
            worker->postStart();
        }
    }
}

void JYDeviceOrchestrator::triggerAll()
{
    for (auto *worker : m_workers) {
        if (worker) {
            worker->postTrigger();
        }
    }
}

void JYDeviceOrchestrator::stopAll()
{
    for (auto *worker : m_workers) {
        if (worker) {
            worker->postStop();
        }
    }
}

void JYDeviceOrchestrator::closeAll()
{
    for (auto *worker : m_workers) {
        if (worker) {
            worker->postClose();
        }
    }
}

bool JYDeviceOrchestrator::synchronizeStart(const JYDeviceConfig &config532x,
                                            const JYDeviceConfig &config5711,
                                            const JYDeviceConfig &config8902,
                                            int timeoutMs,
                                            qint64 *barrierReleaseMs)
{
    // 统一启动的关键点是：所有设备先配置完成，再统一 start，再统一 trigger。
    // 这样可以最大限度缩小不同设备真正开始采样/输出的时间偏差。
    m_lastConfig532x = config532x;
    m_lastConfig5711 = config5711;
    m_lastConfig8902 = config8902;
    m_hasLastConfig = true;

    configureAll(config532x, config5711, config8902);
    QString reason;
    if (!waitForAll(JYDeviceState::Configured, timeoutMs, &reason, config532x, config5711, config8902, true)) {
        closeAll();
        emit syncFailed(reason);
        return false;
    }

    for (auto *worker : m_workers) {
        if (!worker) {
            continue;
        }
        if (!isEnabled(worker->kind(), config532x, config5711, config8902)) {
            continue;
        }
        worker->postStart();
    }

    if (!waitForAll(JYDeviceState::Armed, timeoutMs, &reason, config532x, config5711, config8902, true)) {
        closeAll();
        emit syncFailed(reason);
        return false;
    }

    const qint64 releaseTs = QDateTime::currentMSecsSinceEpoch();
    if (barrierReleaseMs) {
        *barrierReleaseMs = releaseTs;
    }

    for (auto *worker : m_workers) {
        if (!worker) {
            continue;
        }
        if (!isEnabled(worker->kind(), config532x, config5711, config8902)) {
            continue;
        }
        worker->postTrigger();
    }

    if (!waitForAll(JYDeviceState::Running, timeoutMs, &reason, config532x, config5711, config8902, true)) {
        closeAll();
        emit syncFailed(reason);
        return false;
    }

    emit syncSucceeded();
    return true;
}

bool JYDeviceOrchestrator::synchronizeStop(int timeoutMs)
{
    // 若有最近一次启动配置，则只停止当次参与运行的设备，避免误伤未启用设备。
    QString reason;
    if (m_hasLastConfig) {
        for (auto *worker : m_workers) {
            if (!worker) {
                continue;
            }
            if (!isEnabled(worker->kind(), m_lastConfig532x, m_lastConfig5711, m_lastConfig8902)) {
                continue;
            }
            worker->postStop();
        }
        if (!waitForAll(JYDeviceState::Configured, timeoutMs, &reason, m_lastConfig532x, m_lastConfig5711, m_lastConfig8902, true)) {
            closeAll();
            emit syncFailed(reason);
            return false;
        }
        emit syncSucceeded();
        return true;
    }

    stopAll();
    if (!waitForAll(JYDeviceState::Configured, timeoutMs, &reason, JYDeviceConfig{}, JYDeviceConfig{}, JYDeviceConfig{}, false)) {
        closeAll();
        emit syncFailed(reason);
        return false;
    }
    emit syncSucceeded();
    return true;
}

bool JYDeviceOrchestrator::waitForAll(JYDeviceState targetState,
                                      int timeoutMs,
                                      QString *reason,
                                      const JYDeviceConfig &config532x,
                                      const JYDeviceConfig &config5711,
                                      const JYDeviceConfig &config8902,
                                      bool useFilter)
{
    // 这里通过事件循环等待 worker 的状态信号，避免 UI 线程忙等轮询。
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    auto isDone = [this, targetState, &config532x, &config5711, &config8902, useFilter]() -> bool {
        if (m_workers.isEmpty()) {
            return true;
        }
        for (auto *worker : m_workers) {
            if (!worker) {
                continue;
            }
            if (useFilter && !isEnabled(worker->kind(), config532x, config5711, config8902)) {
                continue;
            }
            const JYDeviceState state = m_states.value(worker, worker->state());
            if (state == JYDeviceState::Faulted) {
                return false;
            }
            if (state != targetState) {
                return false;
            }
        }
        return true;
    };

    if (isDone()) {
        return true;
    }

    if (timeoutMs <= 0) {
        timeoutMs = 3000;
    }

    QList<QMetaObject::Connection> stateConnections;
    stateConnections.reserve(m_workers.size());

    auto quitIfReady = [&]() {
        if (isDone()) {
            loop.quit();
        }
    };

    for (auto *worker : m_workers) {
        if (!worker) {
            continue;
        }
        stateConnections.push_back(
            connect(worker, &JYDeviceWorker::statusChanged, &loop,
                    [&](JYDeviceKind, JYDeviceState, const QString &) {
                        quitIfReady();
                    }));
    }

    timer.start(timeoutMs);
    loop.exec(QEventLoop::AllEvents);

    for (const auto &connection : stateConnections) {
        disconnect(connection);
    }

    if (isDone()) {
        timer.stop();
        return true;
    }

    if (reason) {
        *reason = QStringLiteral("timeout waiting for devices");
    }
    return false;
}
