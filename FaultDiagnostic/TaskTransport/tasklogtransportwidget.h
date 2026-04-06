#ifndef TASKLOGTRANSPORTWIDGET_H
#define TASKLOGTRANSPORTWIDGET_H

#include "tasklogtransportsettings.h"

#include <QWidget>

class QJsonObject;
class QTableWidget;
class QTcpServer;
class QTcpSocket;
class QUdpSocket;

namespace Ui {
class TaskLogTransportWidget;
}

class TaskLogTransportWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TaskLogTransportWidget(QWidget *parent = nullptr);
    ~TaskLogTransportWidget();

private:
    void loadSettingsToUi();
    TaskLogTransportSettings settingsFromUi() const;
    void clearReceivedTable();
    void appendReceivedPayload(const QString &transport, const QByteArray &payload);
    void appendReceivedRow(const QString &transport,
                           const QString &deviceName,
                           int totalBoards,
                           const QString &recordTime,
                           const QString &recordsSummary,
                           const QString &rawJson);
    void stopListener();
    bool startListener(QString *errorMessage = nullptr);
    void handleTcpConnection();
    void updateTestStatus(const QString &message, bool isError = false);

    Ui::TaskLogTransportWidget *ui;
    QTcpServer *m_tcpServer = nullptr;
    QUdpSocket *m_udpSocket = nullptr;
};

#endif // TASKLOGTRANSPORTWIDGET_H
