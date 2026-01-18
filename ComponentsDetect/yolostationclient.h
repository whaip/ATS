#ifndef YOLOSTATIONCLIENT_H
#define YOLOSTATIONCLIENT_H

#include <QObject>
#include <QImage>
#include <QPolygonF>
#include <QString>

#include "componenttypes.h"
#include "yolostation.h"

class YoloStationClient : public QObject
{
    Q_OBJECT
public:
    explicit YoloStationClient(const QString &tag, QObject *parent = nullptr);

    void setEnabled(bool enabled);
    void setUsePcbExtract(bool enabled);
    void setClassDisplay(const QList<int> &classIds);

public slots:
    void submitFrame(const QImage &frame, qint64 timestampMs);

signals:
    void resultReady(const QImage &frame,
                     const QList<CompLabel> &labels,
                     const QPolygonF &pcbQuad,
                     qint64 timestampMs,
                     double inferMs);

private:
    QString m_tag;
    bool m_enabled = false;
    bool m_usePcbExtract = false;
    QList<int> m_classDisplay;
};

#endif // YOLOSTATIONCLIENT_H
