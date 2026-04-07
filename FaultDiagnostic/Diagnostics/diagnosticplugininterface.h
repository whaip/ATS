#ifndef DIAGNOSTICPLUGININTERFACE_H
#define DIAGNOSTICPLUGININTERFACE_H

#include <QObject>
#include <QString>

#include "diagnosticdatatypes.h"

class DiagnosticPluginInterface
{
public:
    virtual ~DiagnosticPluginInterface() = default;

    // 插件元信息与适用元件类型。
    virtual QString pluginId() const = 0;
    virtual QString displayName() const = 0;
    virtual QString version() const = 0;
    virtual QString componentType() const = 0;

    // 对已经采集完成的数据做诊断，并输出统一格式的报告。
    virtual DiagnosticReport diagnose(const DiagnosticInput &input, QString *error) const = 0;
};

#define DiagnosticPluginInterface_iid "com.faultdetect.diagnosticplugin/1.0"
Q_DECLARE_INTERFACE(DiagnosticPluginInterface, DiagnosticPluginInterface_iid)

#endif // DIAGNOSTICPLUGININTERFACE_H
