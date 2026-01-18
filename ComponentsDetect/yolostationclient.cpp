#include "yolostationclient.h"

#include <QtGlobal>

YoloStationClient::YoloStationClient(const QString &tag, QObject *parent)
    : QObject(parent)
    , m_tag(tag)
{
    YoloStation::instance()->start();

    connect(YoloStation::instance(), &YoloStation::resultReady, this,
            [this](const YoloStation::Result &r) {
                if (r.tag != m_tag) {
                    return;
                }
                emit resultReady(r.frame, r.labels, r.pcbQuad, r.timestampMs, r.inferMs);
            },
            Qt::QueuedConnection);
}

void YoloStationClient::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void YoloStationClient::setUsePcbExtract(bool enabled)
{
    m_usePcbExtract = enabled;
}

void YoloStationClient::setClassDisplay(const QList<int> &classIds)
{
    m_classDisplay = classIds;
}

void YoloStationClient::submitFrame(const QImage &frame, qint64 timestampMs)
{
    if (!m_enabled) {
        return;
    }

    YoloStation::Task t;
    t.tag = m_tag;
    t.frame = frame;
    t.timestampMs = timestampMs;
    t.usePcbExtract = m_usePcbExtract;
    t.classDisplay = m_classDisplay;
    t.enabled = m_enabled;

    YoloStation::instance()->submitTask(t);
}
