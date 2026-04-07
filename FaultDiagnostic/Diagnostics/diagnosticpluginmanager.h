#ifndef DIAGNOSTICPLUGINMANAGER_H
#define DIAGNOSTICPLUGINMANAGER_H

#include <QObject>
#include <QMap>
#include <QVector>

#include "diagnosticplugininterface.h"

class QPluginLoader;

// 诊断插件管理器，统一管理内建插件和动态加载插件。
class DiagnosticPluginManager : public QObject
{
    Q_OBJECT
public:
    explicit DiagnosticPluginManager(QObject *parent = nullptr);
    ~DiagnosticPluginManager() override;

    // 设置诊断插件搜索目录。
    void setPluginDir(const QString &path);
    QString pluginDir() const;

    // 注册内建诊断插件，优先用于工程内直接编译进来的实现。
    void addBuiltin(DiagnosticPluginInterface *plugin);

    // 扫描目录并加载全部可用诊断插件。
    bool loadAll(QString *error);
    QVector<QString> pluginIds() const;
    DiagnosticPluginInterface *plugin(const QString &pluginId) const;
    // 按元件类型查找对应的诊断插件。
    DiagnosticPluginInterface *pluginForComponent(const QString &componentType) const;

private:
    void clearLoaders();

    QString m_pluginDir;
    QMap<QString, DiagnosticPluginInterface *> m_builtins;
    QMap<QString, DiagnosticPluginInterface *> m_plugins;
    QVector<QPluginLoader *> m_loaders;
};

#endif // DIAGNOSTICPLUGINMANAGER_H
