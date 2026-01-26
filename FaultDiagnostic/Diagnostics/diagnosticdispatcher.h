#ifndef DIAGNOSTICDISPATCHER_H
#define DIAGNOSTICDISPATCHER_H

#include <QMap>
#include <QString>

#include "diagnosticalgorithm.h"
#include "diagnosticpluginmanager.h"

class DiagnosticDispatcher
{
public:
    void setPluginManager(DiagnosticPluginManager *manager);
    void registerAlgorithm(DiagnosticAlgorithm *algorithm);
    DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const;

private:
    DiagnosticPluginManager *m_pluginManager = nullptr;
    QMap<QString, DiagnosticAlgorithm *> m_algorithms;
};

#endif // DIAGNOSTICDISPATCHER_H
