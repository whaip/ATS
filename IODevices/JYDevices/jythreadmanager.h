#ifndef JYTHREADMANAGER_H
#define JYTHREADMANAGER_H

#include <QObject>
#include <QMap>
#include <QVector>

#include "jydeviceorchestrator.h"
#include "jydatapipeline.h"

class JYThreadManager : public QObject
{
    Q_OBJECT
public:
    explicit JYThreadManager(QObject *parent = nullptr);
    ~JYThreadManager() override;

    JYDeviceWorker *create532xWorker(JYDeviceKind kind);
    JYDeviceWorker *create5711Worker();
    JYDeviceWorker *create8902Worker();

    JYDeviceOrchestrator *orchestrator() const;
    JYDataPipeline *pipeline() const;

    bool isDeviceInitialized(JYDeviceKind kind) const;

    void shutdown();

private:
    QVector<JYDeviceWorker *> m_workers;
    JYDeviceOrchestrator *m_orchestrator = nullptr;
    JYDataPipeline *m_pipeline = nullptr;
    QMap<JYDeviceKind, bool> m_initialized;
};

#endif // JYTHREADMANAGER_H
