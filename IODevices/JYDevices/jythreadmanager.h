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

    // 创建并启动 PXIe-5322 / PXIe-5323 采集工作线程；若已存在则直接返回现有实例。
    JYDeviceWorker *create532xWorker(JYDeviceKind kind);
    // 创建并启动 PXIe-5711 波形输出工作线程。
    JYDeviceWorker *create5711Worker();
    // 创建并启动 PXIe-8902 万用表工作线程。
    JYDeviceWorker *create8902Worker();

    // 返回统一编排器，用于执行多设备同步 configure/start/trigger/stop。
    JYDeviceOrchestrator *orchestrator() const;
    // 返回数据管线，用于接收原始包、转发原始数据并输出对齐批次。
    JYDataPipeline *pipeline() const;

    // 查询指定设备是否已经进入可工作的初始化状态。
    bool isDeviceInitialized(JYDeviceKind kind) const;

    // 关闭所有 worker、清理编排器注册表并释放线程资源。
    void shutdown();

signals:
    // 对外转发设备状态变化，便于界面或上层业务统一监听。
    void deviceStatusChanged(JYDeviceKind kind, JYDeviceState state, const QString &message);

private:
    QVector<JYDeviceWorker *> m_workers;
    QMap<JYDeviceKind, JYDeviceWorker *> m_workerByKind;
    JYDeviceOrchestrator *m_orchestrator = nullptr;
    JYDataPipeline *m_pipeline = nullptr;
    QMap<JYDeviceKind, bool> m_initialized;
};

#endif // JYTHREADMANAGER_H
