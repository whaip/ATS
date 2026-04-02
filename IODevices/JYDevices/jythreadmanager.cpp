#include "jythreadmanager.h"

#include "jydeviceadapter.h"

JYThreadManager::JYThreadManager(QObject *parent)
	: QObject(parent)
	, m_orchestrator(new JYDeviceOrchestrator(this))
	, m_pipeline(new JYDataPipeline(this))
{
}

JYThreadManager::~JYThreadManager()
{
	shutdown();
}

JYDeviceWorker *JYThreadManager::create532xWorker(JYDeviceKind kind)
{
    if (auto *existing = m_workerByKind.value(kind, nullptr)) {
        return existing;
    }

    // 创建输入板 worker 后，统一接入 orchestrator 和 pipeline。
	auto adapter = createJY532xAdapter(kind);
	auto *worker = new JYDeviceWorker(std::move(adapter), this);
	worker->start();
	m_workers.push_back(worker);
	m_workerByKind.insert(kind, worker);
	m_initialized[kind] = false;
	m_orchestrator->addWorker(worker);
	connect(worker, &JYDeviceWorker::statusChanged, this,
			[this](JYDeviceKind kind, JYDeviceState state, const QString &message) {
				const bool active = (state == JYDeviceState::Configured
							 || state == JYDeviceState::Armed
							 || state == JYDeviceState::Running);
				if (active) {
					m_initialized[kind] = true;
				} else if (state == JYDeviceState::Faulted) {
					m_initialized[kind] = false;
				}
				emit deviceStatusChanged(kind, state, message);
			});
	connect(worker, &JYDeviceWorker::dataReady, m_pipeline, &JYDataPipeline::ingest, Qt::QueuedConnection);
	return worker;
}

JYDeviceWorker *JYThreadManager::create5711Worker()
{
    if (auto *existing = m_workerByKind.value(JYDeviceKind::PXIe5711, nullptr)) {
        return existing;
    }

    // 5711 主要承担输出，不会像采集卡一样持续向 pipeline 推送有效数据包。
	auto adapter = createJY5711Adapter();
	auto *worker = new JYDeviceWorker(std::move(adapter), this);
	worker->start();
	m_workers.push_back(worker);
	m_workerByKind.insert(JYDeviceKind::PXIe5711, worker);
	m_initialized[JYDeviceKind::PXIe5711] = false;
	m_orchestrator->addWorker(worker);
	connect(worker, &JYDeviceWorker::statusChanged, this,
			[this](JYDeviceKind kind, JYDeviceState state, const QString &message) {
				const bool active = (state == JYDeviceState::Configured
							 || state == JYDeviceState::Armed
							 || state == JYDeviceState::Running);
				if (active) {
					m_initialized[kind] = true;
				} else if (state == JYDeviceState::Faulted) {
					m_initialized[kind] = false;
				}
				emit deviceStatusChanged(kind, state, message);
			});
	connect(worker, &JYDeviceWorker::dataReady, m_pipeline, &JYDataPipeline::ingest, Qt::QueuedConnection);
	return worker;
}

JYDeviceWorker *JYThreadManager::create8902Worker()
{
    if (auto *existing = m_workerByKind.value(JYDeviceKind::PXIe8902, nullptr)) {
        return existing;
    }

    // 8902 读数频率相对较低，但仍复用相同的 worker + pipeline 框架。
	auto adapter = createJY8902Adapter();
	auto *worker = new JYDeviceWorker(std::move(adapter), this);
	worker->start();
	m_workers.push_back(worker);
	m_workerByKind.insert(JYDeviceKind::PXIe8902, worker);
	m_initialized[JYDeviceKind::PXIe8902] = false;
	m_orchestrator->addWorker(worker);
	connect(worker, &JYDeviceWorker::statusChanged, this,
			[this](JYDeviceKind kind, JYDeviceState state, const QString &message) {
				const bool active = (state == JYDeviceState::Configured
							 || state == JYDeviceState::Armed
							 || state == JYDeviceState::Running);
				if (active) {
					m_initialized[kind] = true;
				} else if (state == JYDeviceState::Faulted) {
					m_initialized[kind] = false;
				}
				emit deviceStatusChanged(kind, state, message);
			});
	connect(worker, &JYDeviceWorker::dataReady, m_pipeline, &JYDataPipeline::ingest, Qt::QueuedConnection);
	return worker;
}

JYDeviceOrchestrator *JYThreadManager::orchestrator() const
{
	return m_orchestrator;
}

JYDataPipeline *JYThreadManager::pipeline() const
{
	return m_pipeline;
}

bool JYThreadManager::isDeviceInitialized(JYDeviceKind kind) const
{
	return m_initialized.value(kind, false);
}

void JYThreadManager::shutdown()
{
    // 先请求底层设备关闭，再停止线程，避免线程退出时设备仍占用驱动资源。
	for (auto *worker : m_workers) {
		if (!worker) continue;
		worker->postClose();
		worker->stop();
		worker->deleteLater();
	}
	m_workers.clear();
	m_workerByKind.clear();
	m_initialized.clear();
	if (m_orchestrator) {
		m_orchestrator->clearWorkers();
	}
}
