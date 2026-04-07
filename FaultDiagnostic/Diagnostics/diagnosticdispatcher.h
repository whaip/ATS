#ifndef DIAGNOSTICDISPATCHER_H
#define DIAGNOSTICDISPATCHER_H

#include <QMap>
#include <QString>

#include "diagnosticalgorithm.h"
#include "diagnosticpluginmanager.h"

class DiagnosticDispatcher
{
public:
    // 插件管理器负责按 componentType 查找插件。
    void setPluginManager(DiagnosticPluginManager *manager);
    // 预留算法注册入口，便于在插件之外挂接额外诊断逻辑。
    void registerAlgorithm(DiagnosticAlgorithm *algorithm);
    // 根据输入元件类型选择诊断插件，并返回统一报告。
    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const;

private:
    DiagnosticPluginManager *m_pluginManager = nullptr;
    QMap<QString, DiagnosticAlgorithm *> m_algorithms;
};

#endif // DIAGNOSTICDISPATCHER_H
