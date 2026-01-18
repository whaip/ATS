#ifndef LOGDISPATCHER_H
#define LOGDISPATCHER_H

#include <QObject>
#include <QString>

class LogDispatcher : public QObject
{
    Q_OBJECT

public:
    explicit LogDispatcher(QObject *parent = nullptr) : QObject(parent) {}

signals:
    void logAdded(const QString &time, const QString &level, const QString &message);
};

#endif // LOGDISPATCHER_H
