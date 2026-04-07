#ifndef TPSPLUGININTERFACE_H
#define TPSPLUGININTERFACE_H

#include <QObject>
#include <QString>

#include "tpsmodels.h"

class TPSPluginInterface
{
public:
    virtual ~TPSPluginInterface() = default;

    // 插件元信息，用于注册、显示和版本管理。
    virtual QString pluginId() const = 0;
    virtual QString displayName() const = 0;
    virtual QString version() const = 0;

    // 参数定义和资源需求，供 UI 面板与端口分配流程使用。
    virtual QVector<TPSParamDefinition> parameterDefinitions() const = 0;
    virtual TPSPluginRequirement requirements() const = 0;

    // 根据端口绑定和参数设置生成设备配置计划、接线说明和采集请求。
    virtual bool buildDevicePlan(const QVector<TPSPortBinding> &bindings,
                                 const QMap<QString, QVariant> &settings,
                                 TPSDevicePlan *plan,
                                 QString *error) = 0;

    // configure 负责执行前配置；execute 负责一次测试任务的实际执行与结果回填。
    virtual bool configure(const QMap<QString, QVariant> &settings, QString *error) = 0;
    virtual bool execute(const TPSRequest &request, TPSResult *result, QString *error) = 0;
};

#define TPSPluginInterface_iid "com.faultdetect.tpsplugin/1.0"
Q_DECLARE_INTERFACE(TPSPluginInterface, TPSPluginInterface_iid)

#endif // TPSPLUGININTERFACE_H
