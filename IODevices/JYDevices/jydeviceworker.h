#ifndef JYDEVICEWORKER_H
#define JYDEVICEWORKER_H

#include "jydevicetype.h"
#include "jydeviceadapter.h"
#include "jydatachannel.h"

#include <QObject>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

class JYDeviceWorker : public QObject
{
    Q_OBJECT
public:
    explicit JYDeviceWorker(std::unique_ptr<JYDeviceAdapter> adapter, QObject *parent = nullptr);
    ~JYDeviceWorker() override;

    void start();
    void stop();

    void postConfigure(const JYDeviceConfig &config);
    void postStart();
    void postTrigger();
    void postStop();
    void postClose();

    JYDeviceKind kind() const;
    JYDeviceState state() const;

signals:
    void statusChanged(JYDeviceKind kind, JYDeviceState state, const QString &message);
    void dataReady(const JYDataPacket &packet);

private:
    void runLoop();
    void enqueue(const std::function<void()> &task);
    void setState(JYDeviceState state, const QString &message);
    bool isStateAllowed(std::initializer_list<JYDeviceState> allowed, QString *error, const QString &action) const;
    void rejectAction(const QString &message);

    std::unique_ptr<JYDeviceAdapter> m_adapter;
    std::thread m_thread;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::function<void()>> m_queue;

    bool m_runningThread = false;
    bool m_stopRequested = false;
    bool m_dataLoop = false;

    JYDeviceState m_state = JYDeviceState::Closed;
};

#endif // JYDEVICEWORKER_H
