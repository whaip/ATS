#ifndef CH340_H
#define CH340_H

#include <QByteArray>
#include <QSerialPort>
#include <QString>

#include <mutex>

class CH340
{
public:
    static CH340 *getInstance()
    {
        static CH340 instance;
        return &instance;
    }

    CH340(const CH340 &) = delete;
    CH340 &operator=(const CH340 &) = delete;
    ~CH340();

    bool isOpen();
    bool Open();
    bool Close();
    QString getLastError();

private:
    CH340();

    bool openPort(const QString &portName);
    bool writeCommand(const QString &command);

    QSerialPort *m_serialPort = nullptr;
    QString m_lastError;
    bool m_switchOpened = false;
    std::mutex m_mutex;
};

#endif // CH340_H
