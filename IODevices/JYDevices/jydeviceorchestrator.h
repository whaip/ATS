#ifndef JYDEVICEORCHESTRATOR_H
#define JYDEVICEORCHESTRATOR_H

#include "jydeviceworker.h"

#include <QObject>
#include <QVector>

class JYDeviceOrchestrator : public QObject
{
    Q_OBJECT
public:
    explicit JYDeviceOrchestrator(QObject *parent = nullptr);

    void addWorker(JYDeviceWorker *worker);
    void clearWorkers();

    void configureAll(const JYDeviceConfig &config532x, const JYDeviceConfig &config5711, const JYDeviceConfig &config8902);
    void startAll();
    void triggerAll();
    void stopAll();
    void closeAll();

    bool synchronizeStart(const JYDeviceConfig &config532x,
                          const JYDeviceConfig &config5711,
                          const JYDeviceConfig &config8902,
                          int timeoutMs);
    bool synchronizeStop(int timeoutMs);

signals:
    void syncFailed(const QString &reason);
    void syncSucceeded();

private:
    bool waitForAll(JYDeviceState targetState, int timeoutMs, QString *reason);

    QVector<JYDeviceWorker *> m_workers;
    QMap<JYDeviceWorker *, JYDeviceState> m_states;
};

#endif // JYDEVICEORCHESTRATOR_H
