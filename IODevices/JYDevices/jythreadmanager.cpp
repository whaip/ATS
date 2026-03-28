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
