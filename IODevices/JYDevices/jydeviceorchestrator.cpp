#include "jydeviceorchestrator.h"

#include <QEventLoop>
#include <QTimer>

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
        switch (worker->kind()) {
            case JYDeviceKind::PXIe5322:
            case JYDeviceKind::PXIe5323:
                worker->postConfigure(config532x);
                break;
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
                                            int timeoutMs)
{
    configureAll(config532x, config5711, config8902);
    QString reason;
    if (!waitForAll(JYDeviceState::Configured, timeoutMs, &reason)) {
        closeAll();
        emit syncFailed(reason);
        return false;
    }

    startAll();
    if (!waitForAll(JYDeviceState::Running, timeoutMs, &reason)) {
        closeAll();
        emit syncFailed(reason);
        return false;
    }

    triggerAll();
    emit syncSucceeded();
    return true;
}

bool JYDeviceOrchestrator::synchronizeStop(int timeoutMs)
{
    stopAll();
    QString reason;
    if (!waitForAll(JYDeviceState::Configured, timeoutMs, &reason)) {
        closeAll();
        emit syncFailed(reason);
        return false;
    }
    emit syncSucceeded();
    return true;
}

bool JYDeviceOrchestrator::waitForAll(JYDeviceState targetState, int timeoutMs, QString *reason)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    auto isDone = [this, targetState]() -> bool {
        if (m_workers.isEmpty()) {
            return true;
        }
        for (auto *worker : m_workers) {
            if (!worker) {
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
    timer.start(timeoutMs);

    while (timer.isActive()) {
        if (isDone()) {
            timer.stop();
            return true;
        }
        loop.processEvents(QEventLoop::AllEvents, 50);
    }

    if (reason) {
        *reason = QStringLiteral("timeout waiting for devices");
    }
    return false;
}
