#ifndef IRCAMERASTATIONCLIENT_H
#define IRCAMERASTATIONCLIENT_H

#include "ircamerastation.h"

#include <QObject>

class IRCameraStationClient : public QObject
{
    Q_OBJECT
public:
    explicit IRCameraStationClient(const QString &tag, QObject *parent = nullptr);
    ~IRCameraStationClient() override;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    void subscribePoint(const QString &id, const QPointF &pos);
    void unsubscribePoint(const QString &id);
    void subscribeBox(const QString &id, const QRectF &rect);
    void unsubscribeBox(const QString &id);
    void clearSubscriptions();

    bool tryGetLatestFrame(IRCameraStation::Frame *out) const;

signals:
    void frameReady(const IRCameraStation::Frame &frame);
    void pointTemperatureReady(const IRCameraStation::PointResult &result);
    void boxTemperatureReady(const IRCameraStation::BoxResult &result);

private:
    QString m_tag;
    bool m_enabled = false;
};

#endif // IRCAMERASTATIONCLIENT_H
