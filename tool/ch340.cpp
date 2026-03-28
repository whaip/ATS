#include "ch340.h"

#include <QDebug>
#include <QSerialPortInfo>
#include <QThread>

CH340::CH340()
    : m_serialPort(new QSerialPort())
{
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        if (info.description() == QStringLiteral("USB-SERIAL CH340")) {
            if (openPort(info.portName())) {
                qDebug() << "CH340设备打开成功";
                Close();
            } else {
                qDebug() << "CH340设备打开失败:" << getLastError();
            }
            break;
        }
    }
}

CH340::~CH340()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_serialPort) {
        if (m_serialPort->isOpen()) {
            m_serialPort->close();
        }
        delete m_serialPort;
        m_serialPort = nullptr;
    }
}

bool CH340::openPort(const QString &portName)
{
    if (!m_serialPort) {
        m_lastError = QStringLiteral("serial port object is null");
        return false;
    }

    if (m_serialPort->isOpen()) {
        m_serialPort->close();
    }

    m_serialPort->setPortName(portName);
    m_serialPort->setBaudRate(QSerialPort::Baud9600);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        m_lastError = m_serialPort->errorString();
        return false;
    }

    return true;
}

bool CH340::writeCommand(const QString &command)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_serialPort || !m_serialPort->isOpen()) {
        m_lastError = QStringLiteral("serial port not open");
        return false;
    }

    QByteArray payload = command.toUtf8();
    if (!payload.endsWith("\r\n")) {
        payload.append("\r\n");
    }

    if (m_serialPort->write(payload) == -1) {
        m_lastError = m_serialPort->errorString();
        return false;
    }

    // Avoid waitForBytesWritten() here because this code runs inside std::thread workers.
    m_serialPort->flush();
    QThread::msleep(50);
    return true;
}

bool CH340::isOpen()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_serialPort && m_serialPort->isOpen();
}

bool CH340::Open()
{
    if (m_switchOpened) {
        return true;
    }
    if (!writeCommand(QStringLiteral("FF01FF"))) {
        m_lastError = QStringLiteral("电源打开失败: %1").arg(m_lastError);
        return false;
    }
    m_switchOpened = true;
    qDebug() << "ch340 open";
    return true;
}

bool CH340::Close()
{
    if (!m_serialPort || !m_serialPort->isOpen()) {
        m_switchOpened = false;
        return true;
    }
    if (!writeCommand(QStringLiteral("FF00FF"))) {
        m_lastError = QStringLiteral("电源关闭失败: %1").arg(m_lastError);
        return false;
    }
    m_switchOpened = false;
    qDebug() << "ch340 close";
    return true;
}

QString CH340::getLastError()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}
