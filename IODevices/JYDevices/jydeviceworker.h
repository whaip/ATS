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

    // 启动专属工作线程；所有设备控制动作和读数都在这条线程中串行执行。
    void start();

    // 请求退出工作线程并阻塞等待线程结束。
    void stop();

    // 投递 configure 请求到工作线程。
    void postConfigure(const JYDeviceConfig &config);
    // 投递 start 请求到工作线程。
    void postStart();
    // 投递软触发 trigger 请求到工作线程。
    void postTrigger();
    // 投递 stop 请求到工作线程。
    void postStop();
    // 投递 close 请求到工作线程。
    void postClose();

    JYDeviceKind kind() const;
    JYDeviceState state() const;

signals:
    void statusChanged(JYDeviceKind kind, JYDeviceState state, const QString &message);
    void dataReady(const JYDataPacket &packet);

private:
    // worker 主循环：先处理控制队列，再在 Running 状态下持续调用 adapter->read() 取数。
    void runLoop();
    // 将一个控制动作压入串行执行队列。
    void enqueue(const std::function<void()> &task);
    // 更新内部状态并发出对外状态信号。
    void setState(JYDeviceState state, const QString &message);
    // 校验当前状态是否允许执行目标动作，避免非法状态迁移。
    bool isStateAllowed(std::initializer_list<JYDeviceState> allowed, QString *error, const QString &action) const;
    // 非法动作时直接回报当前状态和错误信息，不真正执行底层调用。
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
