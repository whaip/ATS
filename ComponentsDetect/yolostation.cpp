#include "yolostation.h"

#include "yolomodel.h"
#include "../logger.h"

#include <QCoreApplication>
#include <QThread>

#include <opencv2/opencv.hpp>

#include <array>
#include <algorithm>

namespace {
std::string toKey(const QString &s)
{
    return s.toUtf8().toStdString();
}
}

YoloStation *YoloStation::instance()
{
    static YoloStation *inst = new YoloStation(qApp);
    return inst;
}

YoloStation::YoloStation(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<YoloStation::Result>("YoloStation::Result");
}

YoloStation::~YoloStation()
{
    stop();
}

void YoloStation::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        return;
    }

    m_stopRequested = false;
    m_running = true;

    m_thread = std::make_unique<std::thread>([this]() { runLoop(); });
}

void YoloStation::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running) {
            return;
        }
        m_stopRequested = true;
    }
    m_cv.notify_all();

    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    m_thread.reset();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_running = false;
}

void YoloStation::submitTask(const Task &task)
{
    Task t = task;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        t.seq = ++m_seq;

        const std::string key = toKey(t.tag);
        const bool firstPending = (m_latestByTag.find(key) == m_latestByTag.end());
        m_latestByTag[key] = t;

        // Only push tag when it becomes pending; if already pending, overwrite latest.
        if (firstPending) {
            m_readyTags.push_back(t.tag);
        }
    }
    m_cv.notify_one();
}

bool YoloStation::tryGetLatestResult(const QString &tag, Result *out) const
{
    if (!out) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_latestResultByTag.find(toKey(tag));
    if (it == m_latestResultByTag.end()) {
        return false;
    }
    *out = it->second;
    return true;
}

void YoloStation::runLoop()
{
    emit stationStatus(QStringLiteral("YOLO station started"));

    // Lazy init model in station thread.
    {
        QMutexLocker qtLock(&m_qtMutex);
        if (!m_model) {
            m_model = YOLOModel::getInstance();
        }
    }

    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() { return m_stopRequested || !m_readyTags.empty(); });
            if (m_stopRequested) {
                break;
            }

            // Pop next tag and take its latest task.
            const QString tag = m_readyTags.front();
            m_readyTags.pop_front();

            const std::string key = toKey(tag);
            auto it = m_latestByTag.find(key);
            if (it == m_latestByTag.end()) {
                continue;
            }
            task = it->second;
            m_latestByTag.erase(it);
        }

        if (!task.enabled || task.frame.isNull()) {
            continue;
        }

        // Apply per-task class filter (empty means "all").
        {
            QMutexLocker qtLock(&m_qtMutex);
            if (m_model) {
                std::vector<int> cls;
                if (task.classDisplay.isEmpty()) {
                    const auto &names = m_model->get_class_names();
                    cls.reserve(names.size());
                    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
                        cls.push_back(i);
                    }
                } else {
                    cls.reserve(static_cast<size_t>(task.classDisplay.size()));
                    for (int v : task.classDisplay) {
                        cls.push_back(v);
                    }
                }
                m_model->set_class_display(cls);
            }
        }

        QElapsedTimer timer;
        timer.start();

        QList<CompLabel> labels;
        QPolygonF quad;
        try {
            cv::Mat bgr;
            {
                // Convert in this thread
                QImage rgb = task.frame;
                if (rgb.format() != QImage::Format_RGB888) {
                    rgb = rgb.convertToFormat(QImage::Format_RGB888);
                }
                cv::Mat rgbMat(rgb.height(), rgb.width(), CV_8UC3, const_cast<uchar *>(rgb.bits()), static_cast<size_t>(rgb.bytesPerLine()));
                cv::cvtColor(rgbMat, bgr, cv::COLOR_RGB2BGR);
                bgr = bgr.clone();
            }

            std::array<cv::Point2f,4> pcbQuad{};
            bool extracted = false;

            std::vector<CompLabel> out;
            {
                QMutexLocker qtLock(&m_qtMutex);
                out = m_model->infer(bgr, task.usePcbExtract, &pcbQuad, &extracted);
            }

            labels.reserve(static_cast<int>(out.size()));
            for (const auto &l : out) {
                labels.push_back(l);
            }

            if (extracted) {
                quad.reserve(4);
                for (const auto &p : pcbQuad) {
                    quad << QPointF(p.x, p.y);
                }
            }
        } catch (...) {
            Logger::log(QStringLiteral("YoloStation inference exception"), Logger::Level::Error);
            labels.clear();
            quad.clear();
        }

        Result result;
        result.tag = task.tag;
        result.frame = task.frame;
        result.labels = labels;
        result.pcbQuad = quad;
        result.timestampMs = task.timestampMs;
        result.inferMs = static_cast<double>(timer.elapsed());
        result.seq = task.seq;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latestResultByTag[toKey(result.tag)] = result;
        }

        emit resultReady(result);
        QThread::yieldCurrentThread();
    }

    emit stationStatus(QStringLiteral("YOLO station stopped"));
}
