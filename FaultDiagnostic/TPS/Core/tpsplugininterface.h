#ifndef TPSPLUGININTERFACE_H
#define TPSPLUGININTERFACE_H

#include <QObject>
#include <QString>

#include "tpsmodels.h"

class TPSPluginInterface
{
public:
    virtual ~TPSPluginInterface() = default;

    virtual QString pluginId() const = 0;
    virtual QString displayName() const = 0;
    virtual QString version() const = 0;

    virtual QVector<TPSParamDefinition> parameterDefinitions() const = 0;
    virtual TPSPluginRequirement requirements() const = 0;

    virtual bool buildDevicePlan(const QVector<TPSPortBinding> &bindings,
                                 const QMap<QString, QVariant> &settings,
                                 TPSDevicePlan *plan,
                                 QString *error) = 0;

    virtual bool configure(const QMap<QString, QVariant> &settings, QString *error) = 0;
    virtual bool execute(const TPSRequest &request, TPSResult *result, QString *error) = 0;
};

#define TPSPluginInterface_iid "com.faultdetect.tpsplugin/1.0"
Q_DECLARE_INTERFACE(TPSPluginInterface, TPSPluginInterface_iid)

#endif // TPSPLUGININTERFACE_H
