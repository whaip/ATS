#ifndef TASKLOGTRANSPORTSETTINGS_H
#define TASKLOGTRANSPORTSETTINGS_H

#include <QString>
#include <QtGlobal>

enum class TaskLogTransportProtocol {
    Http,
    Tcp,
    Udp
};

struct TaskLogTransportSettings
{
    bool sendEnabled = true;
    TaskLogTransportProtocol sendProtocol = TaskLogTransportProtocol::Http;
    QString sendHost = QStringLiteral("127.0.0.1");
    quint16 sendPort = 8080;

    TaskLogTransportProtocol testProtocol = TaskLogTransportProtocol::Http;
    QString listenAddress = QStringLiteral("127.0.0.1");
    quint16 listenPort = 8080;

    int broadcastIntervalMs = 1000;
};

QString taskLogTransportProtocolKey(TaskLogTransportProtocol protocol);
QString taskLogTransportProtocolLabel(TaskLogTransportProtocol protocol);
TaskLogTransportProtocol taskLogTransportProtocolFromKey(const QString &key);

TaskLogTransportSettings loadTaskLogTransportSettings();
void saveTaskLogTransportSettings(const TaskLogTransportSettings &settings);

#endif // TASKLOGTRANSPORTSETTINGS_H
