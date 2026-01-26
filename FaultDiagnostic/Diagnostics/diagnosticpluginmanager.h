#ifndef DIAGNOSTICPLUGINMANAGER_H
#define DIAGNOSTICPLUGINMANAGER_H

#include <QObject>
#include <QMap>
#include <QVector>

#include "diagnosticplugininterface.h"

class QPluginLoader;

class DiagnosticPluginManager : public QObject
{
    Q_OBJECT
public:
    explicit DiagnosticPluginManager(QObject *parent = nullptr);
    ~DiagnosticPluginManager() override;

    void setPluginDir(const QString &path);
    QString pluginDir() const;

    void addBuiltin(DiagnosticPluginInterface *plugin);

    bool loadAll(QString *error);
    QVector<QString> pluginIds() const;
    DiagnosticPluginInterface *plugin(const QString &pluginId) const;
    DiagnosticPluginInterface *pluginForComponent(const QString &componentType) const;

private:
    void clearLoaders();

    QString m_pluginDir;
    QMap<QString, DiagnosticPluginInterface *> m_builtins;
    QMap<QString, DiagnosticPluginInterface *> m_plugins;
    QVector<QPluginLoader *> m_loaders;
};

#endif // DIAGNOSTICPLUGINMANAGER_H
