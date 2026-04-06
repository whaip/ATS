#include "tasklogtransportwidget.h"
#include "ui_tasklogtransportwidget.h"
#include "tasklogtransportservice.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QHeaderView>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

namespace {
void fillProtocolCombo(QComboBox *comboBox)
{
    if (!comboBox) {
        return;
    }

    comboBox->clear();
    const TaskLogTransportProtocol protocols[] = {
        TaskLogTransportProtocol::Http,
        TaskLogTransportProtocol::Tcp,
        TaskLogTransportProtocol::Udp
    };
    for (TaskLogTransportProtocol protocol : protocols) {
        comboBox->addItem(taskLogTransportProtocolLabel(protocol), taskLogTransportProtocolKey(protocol));
    }
}

int comboIndexForProtocol(QComboBox *comboBox, TaskLogTransportProtocol protocol)
{
    if (!comboBox) {
        return -1;
    }
    return comboBox->findData(taskLogTransportProtocolKey(protocol));
}

QByteArray httpBodyFromRequest(const QByteArray &payload)
{
    const int separatorIndex = payload.indexOf("\r\n\r\n");
    if (separatorIndex < 0) {
        return payload;
    }
    return payload.mid(separatorIndex + 4);
}

QString recordsSummaryText(const QJsonArray &records)
{
    QStringList parts;
    for (const QJsonValue &value : records) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) {
            continue;
        }
        parts.append(QStringLiteral("%1:%2").arg(name).arg(object.value(QStringLiteral("count")).toInt()));
    }
    return parts.join(QStringLiteral("；"));
}

QString safeJsonString(const QJsonObject &object, const QString &key)
{
    return object.value(key).toString().trimmed();
}
}

TaskLogTransportWidget::TaskLogTransportWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TaskLogTransportWidget)
{
    ui->setupUi(this);

    fillProtocolCombo(ui->sendProtocolCombo);
    fillProtocolCombo(ui->testProtocolCombo);
    ui->sendHostEdit->setText(QStringLiteral("127.0.0.1"));
    ui->listenAddressEdit->setText(QStringLiteral("127.0.0.1"));
    ui->sendPortSpin->setRange(1, 65535);
    ui->testPortSpin->setRange(1, 65535);
    ui->sendPortSpin->setValue(8080);
    ui->testPortSpin->setValue(8080);
    ui->sendPortSpin->setReadOnly(false);
    ui->testPortSpin->setReadOnly(false);

    ui->receivedTable->setColumnCount(6);
    ui->receivedTable->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("接收时间")
                      << QStringLiteral("协议")
                      << QStringLiteral("检测设备名称")
                      << QStringLiteral("板卡总数")
                      << QStringLiteral("记录时间")
                      << QStringLiteral("故障统计"));
    ui->receivedTable->horizontalHeader()->setStretchLastSection(true);
    ui->receivedTable->verticalHeader()->setVisible(false);
    ui->receivedTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->receivedTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->receivedTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->receivedTable->setWordWrap(false);

    loadSettingsToUi();

    connect(ui->saveButton, &QPushButton::clicked, this, [this]() {
        saveTaskLogTransportSettings(settingsFromUi());
        updateTestStatus(QStringLiteral("发送设置已保存"), false);
    });
    connect(ui->startListenButton, &QPushButton::clicked, this, [this]() {
        QString errorMessage;
        if (startListener(&errorMessage)) {
            const TaskLogTransportSettings settings = settingsFromUi();
            updateTestStatus(QStringLiteral("监听已启动，正在接收发送端实时数据：%1 %2:%3")
                                 .arg(taskLogTransportProtocolLabel(settings.testProtocol),
                                      settings.listenAddress.trimmed(),
                                      QString::number(settings.listenPort)),
                             false);
        } else {
            updateTestStatus(QStringLiteral("监听启动失败：%1").arg(errorMessage), true);
        }
    });
    connect(ui->stopListenButton, &QPushButton::clicked, this, [this]() {
        stopListener();
        updateTestStatus(QStringLiteral("监听已停止"), false);
    });
    connect(ui->clearButton, &QPushButton::clicked, this, [this]() {
        clearReceivedTable();
        updateTestStatus(QStringLiteral("接收表格已清空"), false);
    });
}

TaskLogTransportWidget::~TaskLogTransportWidget()
{
    stopListener();
    delete ui;
}

void TaskLogTransportWidget::loadSettingsToUi()
{
    const TaskLogTransportSettings settings = loadTaskLogTransportSettings();
    ui->sendEnabledCheckBox->setChecked(settings.sendEnabled);
    ui->sendHostEdit->setText(settings.sendHost);
    ui->sendPortSpin->setValue(settings.sendPort);
    ui->listenAddressEdit->setText(settings.listenAddress);
    ui->testPortSpin->setValue(settings.listenPort);

    const int sendIndex = comboIndexForProtocol(ui->sendProtocolCombo, settings.sendProtocol);
    if (sendIndex >= 0) {
        ui->sendProtocolCombo->setCurrentIndex(sendIndex);
    }

    const int testIndex = comboIndexForProtocol(ui->testProtocolCombo, settings.testProtocol);
    if (testIndex >= 0) {
        ui->testProtocolCombo->setCurrentIndex(testIndex);
    }
}

TaskLogTransportSettings TaskLogTransportWidget::settingsFromUi() const
{
    TaskLogTransportSettings settings;
    settings.sendEnabled = ui->sendEnabledCheckBox->isChecked();
    settings.sendProtocol = taskLogTransportProtocolFromKey(ui->sendProtocolCombo->currentData().toString());
    settings.sendHost = ui->sendHostEdit->text().trimmed();
    settings.sendPort = static_cast<quint16>(ui->sendPortSpin->value());
    settings.testProtocol = taskLogTransportProtocolFromKey(ui->testProtocolCombo->currentData().toString());
    settings.listenAddress = ui->listenAddressEdit->text().trimmed();
    settings.listenPort = static_cast<quint16>(ui->testPortSpin->value());
    return settings;
}

void TaskLogTransportWidget::clearReceivedTable()
{
    ui->receivedTable->setRowCount(0);
}

void TaskLogTransportWidget::appendReceivedPayload(const QString &transport, const QByteArray &payload)
{
    const QByteArray trimmedPayload = payload.trimmed();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmedPayload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        appendReceivedRow(transport,
                          QStringLiteral("解析失败"),
                          0,
                          QString(),
                          QStringLiteral("原始数据不是有效统计 JSON"),
                          QString::fromUtf8(trimmedPayload));
        updateTestStatus(QStringLiteral("已收到数据，但解析失败"), true);
        return;
    }

    const QJsonObject object = document.object();
    appendReceivedRow(transport,
                      safeJsonString(object, QStringLiteral("detectionDeviceName")),
                      object.value(QStringLiteral("totalDetectedBoards")).toInt(),
                      safeJsonString(object, QStringLiteral("recordTime")),
                      recordsSummaryText(object.value(QStringLiteral("records")).toArray()),
                      QString::fromUtf8(trimmedPayload));
    updateTestStatus(QStringLiteral("已收到发送端实时统计数据"), false);
}

void TaskLogTransportWidget::appendReceivedRow(const QString &transport,
                                               const QString &deviceName,
                                               int totalBoards,
                                               const QString &recordTime,
                                               const QString &recordsSummary,
                                               const QString &rawJson)
{
    const int row = ui->receivedTable->rowCount();
    ui->receivedTable->insertRow(row);
    ui->receivedTable->setItem(row, 0, new QTableWidgetItem(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    ui->receivedTable->setItem(row, 1, new QTableWidgetItem(transport));
    ui->receivedTable->setItem(row, 2, new QTableWidgetItem(deviceName));
    ui->receivedTable->setItem(row, 3, new QTableWidgetItem(QString::number(totalBoards)));
    ui->receivedTable->setItem(row, 4, new QTableWidgetItem(recordTime));
    auto *summaryItem = new QTableWidgetItem(recordsSummary);
    summaryItem->setToolTip(rawJson);
    ui->receivedTable->setItem(row, 5, summaryItem);
    ui->receivedTable->scrollToBottom();
}

void TaskLogTransportWidget::stopListener()
{
    if (m_udpSocket) {
        m_udpSocket->close();
        m_udpSocket->deleteLater();
        m_udpSocket = nullptr;
    }
    if (m_tcpServer) {
        m_tcpServer->close();
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }
}

bool TaskLogTransportWidget::startListener(QString *errorMessage)
{
    stopListener();

    const TaskLogTransportSettings settings = settingsFromUi();
    const QHostAddress address(settings.listenAddress);
    if (address.isNull()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("监听地址无效");
        }
        return false;
    }

    if (settings.testProtocol == TaskLogTransportProtocol::Udp) {
        m_udpSocket = new QUdpSocket(this);
        if (!m_udpSocket->bind(address,
                               settings.listenPort,
                               QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            if (errorMessage) {
                *errorMessage = m_udpSocket->errorString();
            }
            stopListener();
            return false;
        }
        connect(m_udpSocket, &QUdpSocket::readyRead, this, [this]() {
            while (m_udpSocket && m_udpSocket->hasPendingDatagrams()) {
                QByteArray datagram;
                datagram.resize(static_cast<int>(m_udpSocket->pendingDatagramSize()));
                QHostAddress sender;
                quint16 senderPort = 0;
                m_udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
                appendReceivedPayload(QStringLiteral("UDP %1:%2")
                                          .arg(sender.toString(), QString::number(senderPort)),
                                      datagram);
            }
        });
        return true;
    }

    m_tcpServer = new QTcpServer(this);
    if (!m_tcpServer->listen(address, settings.listenPort)) {
        if (errorMessage) {
            *errorMessage = m_tcpServer->errorString();
        }
        stopListener();
        return false;
    }
    connect(m_tcpServer, &QTcpServer::newConnection, this, &TaskLogTransportWidget::handleTcpConnection);
    return true;
}

void TaskLogTransportWidget::handleTcpConnection()
{
    if (!m_tcpServer) {
        return;
    }

    while (m_tcpServer->hasPendingConnections()) {
        QTcpSocket *socket = m_tcpServer->nextPendingConnection();
        if (!socket) {
            continue;
        }

        socket->setProperty("transportBuffer", QByteArray());

        connect(socket, &QTcpSocket::readyRead, this, [socket]() {
            QByteArray buffer = socket->property("transportBuffer").toByteArray();
            buffer.append(socket->readAll());
            socket->setProperty("transportBuffer", buffer);
        });

        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            const TaskLogTransportProtocol protocol =
                taskLogTransportProtocolFromKey(ui->testProtocolCombo->currentData().toString());
            const QByteArray payload = socket->property("transportBuffer").toByteArray();
            const QByteArray displayPayload =
                protocol == TaskLogTransportProtocol::Http ? httpBodyFromRequest(payload) : payload;
            appendReceivedPayload(QStringLiteral("%1 %2:%3")
                                      .arg(taskLogTransportProtocolLabel(protocol),
                                           socket->peerAddress().toString(),
                                           QString::number(socket->peerPort())),
                                  displayPayload);
            socket->deleteLater();
        });

        if (taskLogTransportProtocolFromKey(ui->testProtocolCombo->currentData().toString()) == TaskLogTransportProtocol::Http) {
            connect(socket, &QTcpSocket::readyRead, this, [socket]() {
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");
                socket->flush();
                socket->disconnectFromHost();
            });
        }
    }
}

void TaskLogTransportWidget::updateTestStatus(const QString &message, bool isError)
{
    ui->testStatusLabel->setText(message);
    ui->testStatusLabel->setStyleSheet(isError
                                           ? QStringLiteral("color:#c0392b;")
                                           : QStringLiteral("color:#2e7d32;"));
}
