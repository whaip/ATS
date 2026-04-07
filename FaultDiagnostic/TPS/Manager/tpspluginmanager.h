#ifndef TPSPLUGINMANAGER_H
#define TPSPLUGINMANAGER_H

#include <QObject>
#include <QMap>
#include <QVector>

#include "../Core/tpsplugininterface.h"

class QPluginLoader;

// TPS 插件管理器，统一管理内建策略插件和目录扫描得到的插件。
class TPSPluginManager : public QObject
{
    Q_OBJECT
public:
    explicit TPSPluginManager(QObject *parent = nullptr);
    ~TPSPluginManager() override;

    // 设置 TPS 插件搜索目录。
    void setPluginDir(const QString &path);
    QString pluginDir() const;

    // 注册内建 TPS 插件。
    void addBuiltin(TPSPluginInterface *plugin);

    // 扫描目录并加载全部可用 TPS 插件。
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
