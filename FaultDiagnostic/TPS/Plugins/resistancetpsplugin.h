#ifndef RESISTANCETPSPLUGIN_H
#define RESISTANCETPSPLUGIN_H

#include <QObject>

#include "../Core/tpsplugininterface.h"

class ResistanceTpsPlugin : public QObject, public TPSPluginInterface
{
    Q_OBJECT
    Q_INTERFACES(TPSPluginInterface)

public:
    explicit ResistanceTpsPlugin(QObject *parent = nullptr);
    QString pluginId() const override;
    QString displayName() const override;
    QString version() const override;

    QVector<TPSParamDefinition> parameterDefinitions() const override;

    bool configure(const QMap<QString, QVariant> &settings, QString *error) override;
    bool execute(const TPSRequest &request, TPSResult *result, QString *error) override;

private:
    QMap<QString, QVariant> m_settings;
};

#endif // RESISTANCETPSPLUGIN_H
