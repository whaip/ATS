#ifndef JYDATACHANNEL_H
#define JYDATACHANNEL_H

#include <deque>
#include <mutex>

template <typename T>
class JYDataChannel
{
public:
    // 追加一条数据到线程安全队列尾部。
    void push(const T &value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(value);
    }

    // 取出最旧的一条数据；若队列为空则返回 false。
    bool tryPop(T *out)
    {
        if (!out) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        *out = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }

    // 直接取出最新一条数据，并清空队列中的历史数据。
    bool tryPopLatest(T *out)
    {
        if (!out) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        *out = std::move(m_queue.back());
        m_queue.clear();
        return true;
    }

    // 清空队列。
    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
    }

    // 返回当前缓存条数。
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

private:
    mutable std::mutex m_mutex;
    std::deque<T> m_queue;
};

#endif // JYDATACHANNEL_H
