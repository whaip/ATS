#ifndef TPSPLUGINMANAGER_H
#define TPSPLUGINMANAGER_H

#include <QObject>
#include <QMap>
#include <QVector>

#include "../Core/tpsplugininterface.h"

class QPluginLoader;

class TPSPluginManager : public QObject
{
    Q_OBJECT
public:
    explicit TPSPluginManager(QObject *parent = nullptr);
    ~TPSPluginManager() override;

    void setPluginDir(const QString &path);
    QString pluginDir() const;

    void addBuiltin(TPSPluginInterface *plugin);

    bool loadAll(QString *error);
    QVector<QString> pluginIds() const;
    TPSPluginInterface *plugin(const QString &pluginId) const;

private:
    void clearLoaders();

    QString m_pluginDir;
    QMap<QString, TPSPluginInterface *> m_builtins;
    QMap<QString, TPSPluginInterface *> m_plugins;
    QVector<QPluginLoader *> m_loaders;
};

#endif // TPSPLUGINMANAGER_H
