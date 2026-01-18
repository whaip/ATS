#ifndef YOLOSTATION_H
#define YOLOSTATION_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QPolygonF>
#include <QString>

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "componenttypes.h"

class YOLOModel;

class YoloStation : public QObject
{
    Q_OBJECT
public:
    struct Task {
        QString tag;
        QImage frame;
        qint64 timestampMs = 0;
        bool usePcbExtract = false;
        QList<int> classDisplay;
        bool enabled = true;
        quint64 seq = 0;
    };

    struct Result {
        QString tag;
        QImage frame;
        QList<CompLabel> labels;
        QPolygonF pcbQuad;
        qint64 timestampMs = 0;
        double inferMs = 0.0;
        quint64 seq = 0;
    };

    static YoloStation *instance();

    // Starts worker thread once; safe to call multiple times.
    void start();
    // Optional: stop thread on app exit.
    void stop();

    // Enqueue/overwrite latest task for tag.
    void submitTask(const Task &task);

    // Fetch latest result for a tag (snapshot). Returns false if none.
    bool tryGetLatestResult(const QString &tag, Result *out) const;

signals:
    void resultReady(const YoloStation::Result &result);
    void stationStatus(const QString &text);

private:
    explicit YoloStation(QObject *parent = nullptr);
    ~YoloStation() override;

    void runLoop();

    mutable QMutex m_qtMutex;

    std::shared_ptr<YOLOModel> m_model;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;

    bool m_running = false;
    bool m_stopRequested = false;

    // Global FIFO of tags to process.
    std::deque<QString> m_readyTags;
    // Latest task per tag.
    std::unordered_map<std::string, Task> m_latestByTag;
    // Latest result per tag.
    std::unordered_map<std::string, Result> m_latestResultByTag;

    quint64 m_seq = 0;

    std::unique_ptr<std::thread> m_thread;
};

Q_DECLARE_METATYPE(YoloStation::Result)

#endif // YOLOSTATION_H
