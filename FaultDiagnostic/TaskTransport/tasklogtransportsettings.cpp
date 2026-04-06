#include "tasklogtransportsettings.h"

#include <QSettings>

namespace {
QString settingsGroup()
{
    return QStringLiteral("TaskLogTransport");
}
}

QString taskLogTransportProtocolKey(TaskLogTransportProtocol protocol)
{
    switch (protocol) {
    case TaskLogTransportProtocol::Tcp:
        return QStringLiteral("tcp");
    case TaskLogTransportProtocol::Udp:
        return QStringLiteral("udp");
    case TaskLogTransportProtocol::Http:
    default:
        return QStringLiteral("http");
    }
}

QString taskLogTransportProtocolLabel(TaskLogTransportProtocol protocol)
{
    switch (protocol) {
    case TaskLogTransportProtocol::Tcp:
        return QStringLiteral("TCP");
    case TaskLogTransportProtocol::Udp:
        return QStringLiteral("UDP");
    case TaskLogTransportProtocol::Http:
    default:
        return QStringLiteral("HTTP");
    }
}

TaskLogTransportProtocol taskLogTransportProtocolFromKey(const QString &key)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("tcp")) {
        return TaskLogTransportProtocol::Tcp;
    }
    if (normalized == QStringLiteral("udp")) {
        return TaskLogTransportProtocol::Udp;
    }
    return TaskLogTransportProtocol::Http;
}

TaskLogTransportSettings loadTaskLogTransportSettings()
{
    TaskLogTransportSettings settings;
    QSettings qsettings;
    qsettings.beginGroup(settingsGroup());
    settings.sendEnabled = qsettings.value(QStringLiteral("sendEnabled"), true).toBool();
    settings.sendProtocol = taskLogTransportProtocolFromKey(
        qsettings.value(QStringLiteral("sendProtocol"), taskLogTransportProtocolKey(TaskLogTransportProtocol::Http)).toString());
    settings.sendHost = qsettings.value(QStringLiteral("sendHost"), settings.sendHost).toString().trimmed();
    settings.sendPort = static_cast<quint16>(qsettings.value(QStringLiteral("sendPort"), settings.sendPort).toUInt());
    settings.testProtocol = taskLogTransportProtocolFromKey(
        qsettings.value(QStringLiteral("testProtocol"), taskLogTransportProtocolKey(TaskLogTransportProtocol::Http)).toString());
    settings.listenAddress = qsettings.value(QStringLiteral("listenAddress"), settings.listenAddress).toString().trimmed();
    settings.listenPort = static_cast<quint16>(qsettings.value(QStringLiteral("listenPort"), settings.listenPort).toUInt());
    settings.broadcastIntervalMs = qsettings.value(QStringLiteral("broadcastIntervalMs"), settings.broadcastIntervalMs).toInt();
    qsettings.endGroup();

    if (settings.sendHost.isEmpty()) {
        settings.sendHost = QStringLiteral("127.0.0.1");
    }
    if (settings.listenAddress.isEmpty()) {
        settings.listenAddress = QStringLiteral("127.0.0.1");
    }
    if (settings.sendPort == 0) {
        settings.sendPort = 8080;
    }
    if (settings.listenPort == 0) {
        settings.listenPort = 8080;
    }
    if (settings.broadcastIntervalMs <= 0) {
        settings.broadcastIntervalMs = 1000;
    }
    return settings;
}

void saveTaskLogTransportSettings(const TaskLogTransportSettings &settings)
{
    QSettings qsettings;
    qsettings.beginGroup(settingsGroup());
    qsettings.setValue(QStringLiteral("sendEnabled"), settings.sendEnabled);
    qsettings.setValue(QStringLiteral("sendProtocol"), taskLogTransportProtocolKey(settings.sendProtocol));
    qsettings.setValue(QStringLiteral("sendHost"), settings.sendHost);
    qsettings.setValue(QStringLiteral("sendPort"), settings.sendPort);
    qsettings.setValue(QStringLiteral("testProtocol"), taskLogTransportProtocolKey(settings.testProtocol));
    qsettings.setValue(QStringLiteral("listenAddress"), settings.listenAddress);
    qsettings.setValue(QStringLiteral("listenPort"), settings.listenPort);
    qsettings.setValue(QStringLiteral("broadcastIntervalMs"), settings.broadcastIntervalMs);
    qsettings.endGroup();
    qsettings.sync();
}
