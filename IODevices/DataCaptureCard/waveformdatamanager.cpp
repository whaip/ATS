#include "waveformdatamanager.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kMinRetentionSeconds = 3.0;
constexpr qint64 kMicrosPerSecond = 1000000;
}

WaveformDataManager::WaveformDataManager(QObject *parent)
    : QObject(parent)
{
    // 视图构建放到并发任务中，避免主线程在大窗口缩放时卡顿。
    m_viewWatcher = new QFutureWatcher<WaveformViewFrame>(this);
    connect(m_viewWatcher, &QFutureWatcher<WaveformViewFrame>::finished, this, [this]() {
        const WaveformViewFrame frame = m_viewWatcher->result();
        emit viewReady(frame);

        bool restart = false;
        {
            QMutexLocker locker(&m_mutex);
            m_viewBuildRunning = false;
            restart = m_pendingRequest.valid;
        }
        if (restart) {
            QMetaObject::invokeMethod(this, [this]() { processPendingView(); }, Qt::QueuedConnection);
        }
    });
}

void WaveformDataManager::setRetentionSeconds(double seconds)
{
    QMutexLocker locker(&m_mutex);
    m_retentionSeconds = qMax(kMinRetentionSeconds, seconds);
}

void WaveformDataManager::setStoragePointBudget(int points)
{
    QMutexLocker locker(&m_mutex);
    m_storagePointBudget = qMax(0, points);
}

void WaveformDataManager::setActiveSeries(const QSet<QString> &seriesKeys)
{
    QMutexLocker locker(&m_mutex);
    m_activeSeries = seriesKeys;

    auto it = m_series.begin();
    while (it != m_series.end()) {
        if (!m_activeSeries.contains(it.key())) {
            it = m_series.erase(it);
        } else {
            ++it;
        }
    }
}

QString WaveformDataManager::seriesKey(JYDeviceKind kind, int channel)
{
    return QStringLiteral("%1:%2").arg(static_cast<int>(kind)).arg(channel);
}

void WaveformDataManager::appendAlignedBatch(const JYAlignedBatch &batch)
{
    QMutexLocker locker(&m_mutex);
    if (m_activeSeries.isEmpty()) {
        return;
    }

    // batch 内已经按设备完成时间对齐，这里只负责拆包并落到统一时间轴缓存。
    for (auto it = batch.packets.cbegin(); it != batch.packets.cend(); ++it) {
        appendPacket(it.key(), it.value());
    }
}

void WaveformDataManager::resetForCapture(quint64 epoch)
{
    QMutexLocker locker(&m_mutex);
    m_epoch = epoch;
    m_series.clear();
    m_pendingRequest = ViewRequest();
}

void WaveformDataManager::setEpoch(quint64 epoch)
{
    QMutexLocker locker(&m_mutex);
    m_epoch = epoch;
}

void WaveformDataManager::requestView(const QStringList &seriesKeys,
                                      double xLower,
                                      double xUpper,
                                      double baseWindowSeconds,
                                      int pixelWidth,
                                      bool autoFollow)
{
    // 这里只记录“最后一次”视图请求，密集缩放时旧请求会被自然覆盖。
    {
        QMutexLocker locker(&m_mutex);
        m_pendingRequest.seriesKeys = seriesKeys;
        m_pendingRequest.xLower = xLower;
        m_pendingRequest.xUpper = xUpper;
        m_pendingRequest.baseWindowSeconds = baseWindowSeconds;
        m_pendingRequest.pixelWidth = pixelWidth;
        m_pendingRequest.autoFollow = autoFollow;
        m_pendingRequest.requestId = m_nextRequestId++;
        m_pendingRequest.valid = true;
    }

    QMetaObject::invokeMethod(this, [this]() { processPendingView(); }, Qt::QueuedConnection);
}

void WaveformDataManager::clear()
{
    QMutexLocker locker(&m_mutex);
    m_series.clear();
    m_pendingRequest = ViewRequest();
}

qint64 WaveformDataManager::reservoirCapacity() const
{
    return static_cast<qint64>(std::llround(m_retentionSeconds * static_cast<double>(kTargetResolutionHz)));
}

void WaveformDataManager::appendRepeated(ChannelSeries *series,
                                         qint64 capacity,
                                         qint64 beginSeq,
                                         qint64 endSeq,
                                         double value)
{
    if (!series || endSeq <= beginSeq || capacity <= 0) {
        return;
    }

    if (!series->hasLastValue) {
        series->lastValue = value;
        series->hasLastValue = true;
    }

    qint64 startSeq = beginSeq;
    if (series->size == 0) {
        series->firstSeq = startSeq;
        series->nextSeq = startSeq;
        series->head = 0;
    } else if (startSeq < series->nextSeq) {
        startSeq = series->nextSeq;
    }

    for (qint64 seq = startSeq; seq < endSeq; ++seq) {
        if (series->size < capacity) {
            const int physical = (series->head + series->size) % static_cast<int>(capacity);
            if (series->reservoir.size() < capacity) {
                series->reservoir.resize(static_cast<int>(capacity));
            }
            series->reservoir[physical] = value;
            ++series->size;
        } else {
            if (series->reservoir.size() < capacity) {
                series->reservoir.resize(static_cast<int>(capacity));
            }
            series->reservoir[series->head] = value;
            series->head = (series->head + 1) % static_cast<int>(capacity);
            ++series->firstSeq;
        }
        series->nextSeq = seq + 1;
    }

    series->lastValue = value;
    series->hasLastValue = true;
}

double WaveformDataManager::valueAt(const ChannelSeries &series, qint64 capacity, qint64 seq)
{
    if (capacity <= 0 || series.size <= 0 || seq < series.firstSeq || seq >= series.nextSeq) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const qint64 offset = seq - series.firstSeq;
    const int physical = (series.head + static_cast<int>(offset)) % static_cast<int>(capacity);
    if (physical < 0 || physical >= series.reservoir.size()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return series.reservoir.at(physical);
}

WaveformViewSeries WaveformDataManager::buildDenseSeries(const QString &key,
                                                         const ChannelSeries &series,
                                                         qint64 capacity,
                                                         qint64 beginSeq,
                                                         qint64 endSeq,
                                                         qint64 windowStartSeq)
{
    WaveformViewSeries out;
    out.key = key;
    if (endSeq <= beginSeq) {
        return out;
    }

    out.x.reserve(static_cast<int>(endSeq - beginSeq));
    out.y.reserve(static_cast<int>(endSeq - beginSeq));
    for (qint64 seq = beginSeq; seq < endSeq; ++seq) {
        const double value = valueAt(series, capacity, seq);
        if (std::isnan(value)) {
            continue;
        }
        out.x.push_back(static_cast<double>(seq - windowStartSeq) / static_cast<double>(kTargetResolutionHz));
        out.y.push_back(value);
    }
    return out;
}

WaveformViewSeries WaveformDataManager::buildBucketedSeries(const QString &key,
                                                            const ChannelSeries &series,
                                                            qint64 capacity,
                                                            qint64 beginSeq,
                                                            qint64 endSeq,
                                                            int pixelWidth,
                                                            qint64 windowStartSeq)
{
    WaveformViewSeries out;
    out.key = key;
    if (endSeq <= beginSeq || pixelWidth <= 0) {
        return out;
    }

    const qint64 count = endSeq - beginSeq;
    const int bucketCount = qMax(1, pixelWidth);
    const double bucketSize = static_cast<double>(count) / static_cast<double>(bucketCount);
    out.x.reserve(bucketCount * 2);
    out.y.reserve(bucketCount * 2);

    for (int bucket = 0; bucket < bucketCount; ++bucket) {
        const qint64 bucketBegin = beginSeq + static_cast<qint64>(bucket * bucketSize);
        const qint64 bucketEnd = qMin(endSeq, beginSeq + static_cast<qint64>((bucket + 1) * bucketSize));
        if (bucketBegin >= bucketEnd) {
            continue;
        }

        double minValue = 0.0;
        double maxValue = 0.0;
        qint64 minSeq = -1;
        qint64 maxSeq = -1;

        for (qint64 seq = bucketBegin; seq < bucketEnd; ++seq) {
            const double value = valueAt(series, capacity, seq);
            if (std::isnan(value)) {
                continue;
            }
            if (minSeq < 0 || value < minValue) {
                minValue = value;
                minSeq = seq;
            }
            if (maxSeq < 0 || value > maxValue) {
                maxValue = value;
                maxSeq = seq;
            }
        }

        if (minSeq < 0 || maxSeq < 0) {
            continue;
        }

        if (minSeq <= maxSeq) {
            out.x.push_back(static_cast<double>(minSeq - windowStartSeq) / static_cast<double>(kTargetResolutionHz));
            out.y.push_back(minValue);
            if (maxSeq != minSeq) {
                out.x.push_back(static_cast<double>(maxSeq - windowStartSeq) / static_cast<double>(kTargetResolutionHz));
                out.y.push_back(maxValue);
            }
        } else {
            out.x.push_back(static_cast<double>(maxSeq - windowStartSeq) / static_cast<double>(kTargetResolutionHz));
            out.y.push_back(maxValue);
            out.x.push_back(static_cast<double>(minSeq - windowStartSeq) / static_cast<double>(kTargetResolutionHz));
            out.y.push_back(minValue);
        }
    }

    return out;
}

WaveformViewFrame WaveformDataManager::buildFrameFromSeries(const QHash<QString, ChannelSeries> &seriesMap,
                                                            const ViewRequest &request,
                                                            quint64 epoch,
                                                            qint64 capacity)
{
    WaveformViewFrame frame;
    frame.requestId = request.requestId;
    frame.epoch = epoch;
    if (!request.valid || request.seriesKeys.isEmpty() || capacity <= 0) {
        return frame;
    }

    qint64 globalLatestSeq = -1;
    bool hasLatest = false;
    for (const QString &key : request.seriesKeys) {
        const auto it = seriesMap.constFind(key);
        if (it == seriesMap.cend() || it->size <= 0) {
            continue;
        }
        const qint64 latestSeq = it->nextSeq - 1;
        if (!hasLatest || latestSeq > globalLatestSeq) {
            globalLatestSeq = latestSeq;
            hasLatest = true;
        }
    }
    if (!hasLatest) {
        return frame;
    }

    const qint64 globalWindowStartSeq = qMax<qint64>(0, globalLatestSeq - capacity + 1);
    const double baseWindow = qMax(kMinRetentionSeconds, request.baseWindowSeconds);
    const qint64 visibleBeginSeq = globalWindowStartSeq
        + static_cast<qint64>(std::floor(qMax(0.0, request.autoFollow ? 0.0 : request.xLower) * kTargetResolutionHz));
    qint64 visibleEndSeq = request.autoFollow
        ? (globalWindowStartSeq + capacity)
        : (globalWindowStartSeq
           + static_cast<qint64>(std::ceil(qMax(request.xLower, request.xUpper) * kTargetResolutionHz)));
    visibleEndSeq = qBound(visibleBeginSeq + 1, visibleEndSeq, globalWindowStartSeq + capacity);

    const int safePixelWidth = qMax(64, request.pixelWidth);
    for (const QString &key : request.seriesKeys) {
        const auto it = seriesMap.constFind(key);
        if (it == seriesMap.cend() || it->size <= 0) {
            continue;
        }

        const ChannelSeries &series = it.value();
        const qint64 seriesBeginSeq = qMax(series.firstSeq, visibleBeginSeq);
        const qint64 seriesEndSeq = qMin(series.nextSeq, visibleEndSeq);
        if (seriesBeginSeq >= seriesEndSeq) {
            continue;
        }

        const qint64 count = seriesEndSeq - seriesBeginSeq;
        // 点数较少时直接输出原始点，点数远大于像素时改为桶化抽稀，兼顾性能和波形形态。
        if (count <= safePixelWidth * 2) {
            frame.series.push_back(buildDenseSeries(key, series, capacity, seriesBeginSeq, seriesEndSeq, globalWindowStartSeq));
        } else {
            frame.series.push_back(buildBucketedSeries(key, series, capacity, seriesBeginSeq, seriesEndSeq, safePixelWidth, globalWindowStartSeq));
        }
    }

    frame.latestX = qMin(baseWindow,
                         static_cast<double>(qMin(capacity, qMax<qint64>(0, globalLatestSeq - globalWindowStartSeq + 1)))
                            / static_cast<double>(kTargetResolutionHz));
    return frame;
}

void WaveformDataManager::processPendingView()
{
    ViewRequest request;
    QHash<QString, ChannelSeries> seriesMap;
    quint64 epoch = 0;
    qint64 capacity = 0;

    {
        QMutexLocker locker(&m_mutex);
        if (m_viewBuildRunning || !m_pendingRequest.valid) {
            return;
        }

        request = m_pendingRequest;
        m_pendingRequest.valid = false;
        m_viewBuildRunning = true;
        epoch = m_epoch;
        capacity = reservoirCapacity();

        for (const QString &key : request.seriesKeys) {
            const auto it = m_series.constFind(key);
            if (it != m_series.cend()) {
                seriesMap.insert(key, it.value());
            }
        }
    }

    // 在后台线程生成当前窗口视图，完成后通过 viewReady 回到界面线程。
    m_viewWatcher->setFuture(QtConcurrent::run([seriesMap, request, epoch, capacity]() {
        return WaveformDataManager::buildFrameFromSeries(seriesMap, request, epoch, capacity);
    }));
}

void WaveformDataManager::appendPacket(JYDeviceKind kind, const JYDataPacket &packet)
{
    if (packet.channelCount <= 0 || packet.samplesPerChannel <= 0 || packet.data.isEmpty()) {
        return;
    }

    const qint64 capacity = reservoirCapacity();
    if (capacity <= 0) {
        return;
    }

    const double sampleRate = qMax(1.0, packet.sampleRateHz);
    const double epochSeconds = static_cast<double>(m_epoch) / 1000.0;
    const double packetStartSeconds = static_cast<double>(packet.timestampMs) / 1000.0 - epochSeconds;
    const qint64 packetStartSeq = qMax<qint64>(0, static_cast<qint64>(std::llround(packetStartSeconds * kTargetResolutionHz)));

    // 不同采样率的数据在这里都被映射到统一 seq 轴，后续显示时就能共用一套逻辑。
    for (int ch = 0; ch < packet.channelCount; ++ch) {
        const QString key = seriesKey(kind, ch);
        if (!m_activeSeries.contains(key)) {
            continue;
        }

        ChannelSeries &series = m_series[key];
        if (series.reservoir.size() != capacity) {
            series.reservoir.resize(static_cast<int>(capacity));
            series.head = 0;
            series.size = 0;
            series.firstSeq = 0;
            series.nextSeq = 0;
            series.hasLastValue = false;
        }

        if (series.hasLastValue && packetStartSeq > series.nextSeq) {
            appendRepeated(&series, capacity, series.nextSeq, packetStartSeq, series.lastValue);
        } else if (series.size == 0) {
            series.firstSeq = packetStartSeq;
            series.nextSeq = packetStartSeq;
        }

        for (int sampleIndex = 0; sampleIndex < packet.samplesPerChannel; ++sampleIndex) {
            const int dataIndex = sampleIndex * packet.channelCount + ch;
            if (dataIndex < 0 || dataIndex >= packet.data.size()) {
                continue;
            }

            const double value = packet.data.at(dataIndex);
            const qint64 beginSeq = packetStartSeq
                + static_cast<qint64>(std::llround((static_cast<double>(sampleIndex) / sampleRate) * kTargetResolutionHz));
            qint64 endSeq = packetStartSeq
                + static_cast<qint64>(std::llround((static_cast<double>(sampleIndex + 1) / sampleRate) * kTargetResolutionHz));
            if (endSeq <= beginSeq) {
                endSeq = beginSeq + 1;
            }
            appendRepeated(&series, capacity, beginSeq, endSeq, value);
        }
    }
}
