#include "jydeviceworker.h"
#include "../../logger.h"

#include <QDateTime>
#include <thread>

JYDeviceWorker::JYDeviceWorker(std::unique_ptr<JYDeviceAdapter> adapter, QObject *parent)
    : QObject(parent)
    , m_adapter(std::move(adapter))
{
}

JYDeviceWorker::~JYDeviceWorker()
{
    stop();
}

void JYDeviceWorker::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_runningThread) {
        return;
    }
    m_runningThread = true;
    m_stopRequested = false;
    m_thread = std::thread(&JYDeviceWorker::runLoop, this);
}

void JYDeviceWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_runningThread) {
            return;
        }
        m_stopRequested = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_runningThread = false;
}

void JYDeviceWorker::postConfigure(const JYDeviceConfig &config)
{
    enqueue([this, config]() {
        Logger::log(QStringLiteral("Device action requested: kind=%1 action=configure")
                        .arg(static_cast<int>(kind())),
                    Logger::Level::Info);
        QString error;
        if (!isStateAllowed({JYDeviceState::Closed, JYDeviceState::Configured, JYDeviceState::Faulted}, &error, QStringLiteral("configure"))) {
            rejectAction(error);
            return;
        }
        if (m_adapter->configure(config, &error)) {
            setState(JYDeviceState::Configured, QStringLiteral("configured"));
        } else {
            setState(JYDeviceState::Faulted, error);
        }
    });
}

void JYDeviceWorker::postStart()
{
    enqueue([this]() {
        Logger::log(QStringLiteral("Device action requested: kind=%1 action=start")
                        .arg(static_cast<int>(kind())),
                    Logger::Level::Info);
        QString error;
        if (!isStateAllowed({JYDeviceState::Configured}, &error, QStringLiteral("start"))) {
            rejectAction(error);
            return;
        }
        if (m_adapter->start(&error)) {
            setState(JYDeviceState::Armed, QStringLiteral("armed"));
        } else {
            setState(JYDeviceState::Faulted, error);
        }
    });
}

void JYDeviceWorker::postTrigger()
{
    enqueue([this]() {
        Logger::log(QStringLiteral("Device action requested: kind=%1 action=trigger")
                        .arg(static_cast<int>(kind())),
                    Logger::Level::Info);
        QString error;
        if (!isStateAllowed({JYDeviceState::Armed, JYDeviceState::Configured, JYDeviceState::Running}, &error, QStringLiteral("trigger"))) {
            rejectAction(error);
            return;
        }
        if (m_adapter->trigger(&error)) {
            // 5711 是输出设备，不需要持续 read；其余输入设备在 trigger 后开启读数循环。
            m_dataLoop = (kind() != JYDeviceKind::PXIe5711);
            setState(JYDeviceState::Running, QStringLiteral("running"));
        } else {
            setState(JYDeviceState::Faulted, error);
        }
    });
}

void JYDeviceWorker::postStop()
{
    enqueue([this]() {
        Logger::log(QStringLiteral("Device action requested: kind=%1 action=stop")
                        .arg(static_cast<int>(kind())),
                    Logger::Level::Info);
        QString error;
        m_dataLoop = false;
        if (!isStateAllowed({JYDeviceState::Running, JYDeviceState::Armed, JYDeviceState::Configured}, &error, QStringLiteral("stop"))) {
            rejectAction(error);
            return;
        }
        if (m_adapter->stop(&error)) {
            setState(JYDeviceState::Configured, QStringLiteral("stopped"));
        } else {
            setState(JYDeviceState::Faulted, error);
        }
    });
}

void JYDeviceWorker::postClose()
{
    enqueue([this]() {
        Logger::log(QStringLiteral("Device action requested: kind=%1 action=close")
                        .arg(static_cast<int>(kind())),
                    Logger::Level::Info);
        QString error;
        m_dataLoop = false;
        if (!isStateAllowed({JYDeviceState::Closed, JYDeviceState::Configured, JYDeviceState::Running, JYDeviceState::Armed, JYDeviceState::Faulted}, &error, QStringLiteral("close"))) {
            rejectAction(error);
            return;
        }
        if (m_adapter->close(&error)) {
            setState(JYDeviceState::Closed, QStringLiteral("closed"));
        } else {
            setState(JYDeviceState::Faulted, error);
        }
    });
}

JYDeviceKind JYDeviceWorker::kind() const
{
    return m_adapter ? m_adapter->kind() : JYDeviceKind::PXIe5322;
}

JYDeviceState JYDeviceWorker::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

void JYDeviceWorker::enqueue(const std::function<void()> &task)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(task);
    }
    m_cv.notify_one();
}

void JYDeviceWorker::setState(JYDeviceState state, const QString &message)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = state;
    }
    Logger::log(QStringLiteral("Device state: kind=%1 state=%2 msg=%3")
                    .arg(static_cast<int>(kind()))
                    .arg(static_cast<int>(state))
                    .arg(message),
                Logger::Level::Info);
    emit statusChanged(kind(), state, message);
}

void JYDeviceWorker::rejectAction(const QString &message)
{
    const JYDeviceState current = state();
    Logger::log(QStringLiteral("Device state: kind=%1 state=%2 msg=%3")
                    .arg(static_cast<int>(kind()))
                    .arg(static_cast<int>(current))
                    .arg(message),
                Logger::Level::Info);
    emit statusChanged(kind(), current, message);
}

bool JYDeviceWorker::isStateAllowed(std::initializer_list<JYDeviceState> allowed, QString *error, const QString &action) const
{
    JYDeviceState current;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        current = m_state;
    }
    for (const auto &state : allowed) {
        if (state == current) {
            return true;
        }
    }
    if (error) {
        *error = QStringLiteral("%1 rejected: state=%2")
                     .arg(action)
                     .arg(static_cast<int>(current));
    }
    return false;
}

void JYDeviceWorker::runLoop()
{
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            // 没有数据循环时以“被控制指令唤醒”为主；有数据循环时则尽快回到 read()。
            if (!m_dataLoop) {
                m_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return m_stopRequested || !m_queue.empty();
                });
            }

            if (m_stopRequested) {
                break;
            }

            if (!m_queue.empty()) {
                task = std::move(m_queue.front());
                m_queue.pop_front();
            }
        }

        if (task) {
            task();
        }

        if (m_dataLoop) {
            // 读数失败分成两类：
            // 1. 有错误字符串：认为底层故障，置 Faulted；
            // 2. 无错误字符串：通常表示当前尚无可读数据，继续下一轮。
            QString error;
            JYDataPacket packet;
            if (m_adapter->read(&packet, &error)) {
                emit dataReady(packet);
            } else if (!error.isEmpty()) {
                Logger::log(QStringLiteral("Read failed: kind=%1 error=%2")
                                .arg(static_cast<int>(kind()))
                                .arg(error),
                            Logger::Level::Warn);
                setState(JYDeviceState::Faulted, error);
                m_dataLoop = false;
            } else if (kind() == JYDeviceKind::PXIe8902) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
}
