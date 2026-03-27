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
	auto adapter = createJY532xAdapter(kind);
	auto *worker = new JYDeviceWorker(std::move(adapter), this);
	worker->start();
	m_workers.push_back(worker);
	m_initialized[kind] = false;
	m_orchestrator->addWorker(worker);
	connect(worker, &JYDeviceWorker::statusChanged, this,
			[this](JYDeviceKind kind, JYDeviceState state, const QString &message) {
				const bool ok = (state == JYDeviceState::Configured
							 || state == JYDeviceState::Armed
							 || state == JYDeviceState::Running);
				m_initialized[kind] = ok;
				emit deviceStatusChanged(kind, state, message);
			});
	connect(worker, &JYDeviceWorker::dataReady, m_pipeline, &JYDataPipeline::ingest, Qt::QueuedConnection);
	return worker;
}

JYDeviceWorker *JYThreadManager::create5711Worker()
{
	auto adapter = createJY5711Adapter();
	auto *worker = new JYDeviceWorker(std::move(adapter), this);
	worker->start();
	m_workers.push_back(worker);
	m_initialized[JYDeviceKind::PXIe5711] = false;
	m_orchestrator->addWorker(worker);
	connect(worker, &JYDeviceWorker::statusChanged, this,
			[this](JYDeviceKind kind, JYDeviceState state, const QString &message) {
				const bool ok = (state == JYDeviceState::Configured
							 || state == JYDeviceState::Armed
							 || state == JYDeviceState::Running);
				m_initialized[kind] = ok;
				emit deviceStatusChanged(kind, state, message);
			});
	connect(worker, &JYDeviceWorker::dataReady, m_pipeline, &JYDataPipeline::ingest, Qt::QueuedConnection);
	return worker;
}

JYDeviceWorker *JYThreadManager::create8902Worker()
{
	auto adapter = createJY8902Adapter();
	auto *worker = new JYDeviceWorker(std::move(adapter), this);
	worker->start();
	m_workers.push_back(worker);
	m_initialized[JYDeviceKind::PXIe8902] = false;
	m_orchestrator->addWorker(worker);
	connect(worker, &JYDeviceWorker::statusChanged, this,
			[this](JYDeviceKind kind, JYDeviceState state, const QString &message) {
				const bool ok = (state == JYDeviceState::Configured
							 || state == JYDeviceState::Armed
							 || state == JYDeviceState::Running);
				m_initialized[kind] = ok;
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
	m_initialized.clear();
	if (m_orchestrator) {
		m_orchestrator->clearWorkers();
	}
}
