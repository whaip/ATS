#include "ircamerastationclient.h"

IRCameraStationClient::IRCameraStationClient(const QString &tag, QObject *parent)
    : QObject(parent)
    , m_tag(tag)
{
    connect(IRCameraStation::instance(), &IRCameraStation::frameReady, this,
            [this](const IRCameraStation::Frame &frame) {
                if (!m_enabled) {
                    return;
                }
                emit frameReady(frame);
            },
            Qt::QueuedConnection);

    connect(IRCameraStation::instance(), &IRCameraStation::pointTemperatureReady, this,
            [this](const IRCameraStation::PointResult &result) {
                if (!m_enabled || result.tag != m_tag) {
                    return;
                }
                emit pointTemperatureReady(result);
            },
            Qt::QueuedConnection);

    connect(IRCameraStation::instance(), &IRCameraStation::boxTemperatureReady, this,
            [this](const IRCameraStation::BoxResult &result) {
                if (!m_enabled || result.tag != m_tag) {
                    return;
                }
                emit boxTemperatureReady(result);
            },
            Qt::QueuedConnection);
}

IRCameraStationClient::~IRCameraStationClient()
{
    clearSubscriptions();
}

void IRCameraStationClient::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool IRCameraStationClient::isEnabled() const
{
    return m_enabled;
}

void IRCameraStationClient::subscribePoint(const QString &id, const QPointF &pos)
{
    IRCameraStation::instance()->setPointSubscription(m_tag, id, pos);
}

void IRCameraStationClient::unsubscribePoint(const QString &id)
{
    IRCameraStation::instance()->removePointSubscription(m_tag, id);
}

void IRCameraStationClient::subscribeBox(const QString &id, const QRectF &rect)
{
    IRCameraStation::instance()->setBoxSubscription(m_tag, id, rect);
}

void IRCameraStationClient::unsubscribeBox(const QString &id)
{
    IRCameraStation::instance()->removeBoxSubscription(m_tag, id);
}

void IRCameraStationClient::clearSubscriptions()
{
    IRCameraStation::instance()->clearSubscriptions(m_tag);
}

bool IRCameraStationClient::tryGetLatestFrame(IRCameraStation::Frame *out) const
{
    return IRCameraStation::instance()->tryGetLatestFrame(out);
}
