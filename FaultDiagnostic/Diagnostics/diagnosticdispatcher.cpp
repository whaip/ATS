#include "diagnosticdispatcher.h"

void DiagnosticDispatcher::setPluginManager(DiagnosticPluginManager *manager)
{
    m_pluginManager = manager;
}

void DiagnosticDispatcher::registerAlgorithm(DiagnosticAlgorithm *algorithm)
{
    if (!algorithm) {
        return;
    }
    const QString type = algorithm->componentType().trimmed();
    if (!type.isEmpty()) {
        m_algorithms.insert(type, algorithm);
    }
}

DiagnosticReport DiagnosticDispatcher::diagnose(const DiagnosticInput &input, QString *error) const
{
    if (m_pluginManager) {
        auto *plugin = m_pluginManager->pluginForComponent(input.componentType);
        if (plugin) {
            return plugin->diagnose(input, error);
        }
    }
    const QString typeKey = input.componentType.trimmed();
    auto *algorithm = m_algorithms.value(typeKey, nullptr);
    if (!algorithm) {
        DiagnosticReport report;
        report.componentRef = input.componentRef;
        report.componentType = input.componentType;
        report.success = false;
        report.summary = QStringLiteral("未找到诊断算法: %1").arg(typeKey);
        if (error) {
            *error = report.summary;
        }
        return report;
    }
    return algorithm->diagnose(input, error);
}
