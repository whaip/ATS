#ifndef JYDATACHANNEL_H
#define JYDATACHANNEL_H

#include <deque>
#include <mutex>

template <typename T>
class JYDataChannel
{
public:
    void push(const T &value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(value);
    }

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

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
    }

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
