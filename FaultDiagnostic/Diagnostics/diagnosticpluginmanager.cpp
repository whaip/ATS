#include "diagnosticpluginmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QPluginLoader>

DiagnosticPluginManager::DiagnosticPluginManager(QObject *parent)
    : QObject(parent)
{
}

DiagnosticPluginManager::~DiagnosticPluginManager()
{
    clearLoaders();
}

void DiagnosticPluginManager::setPluginDir(const QString &path)
{
    m_pluginDir = path;
}

QString DiagnosticPluginManager::pluginDir() const
{
    return m_pluginDir;
}

void DiagnosticPluginManager::addBuiltin(DiagnosticPluginInterface *plugin)
{
    if (!plugin) {
        return;
    }
    const QString id = plugin->pluginId();
    if (id.isEmpty()) {
        return;
    }
    m_builtins.insert(id, plugin);
    m_plugins.insert(id, plugin);
}

bool DiagnosticPluginManager::loadAll(QString *error)
{
    clearLoaders();
    m_plugins = m_builtins;

    QString baseDir = m_pluginDir;
    if (baseDir.isEmpty()) {
        baseDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("diagnostic_plugins"));
    }

    const QDir dir(baseDir);
    if (!dir.exists()) {
        if (error) {
            *error = QStringLiteral("plugin directory not found: %1").arg(baseDir);
        }
        return false;
    }

    const QStringList entries = dir.entryList(QDir::Files);
    for (const auto &fileName : entries) {
        const QString path = dir.filePath(fileName);
        auto *loader = new QPluginLoader(path, this);
        QObject *instance = loader->instance();
        if (!instance) {
            loader->deleteLater();
            continue;
        }
        auto *plugin = qobject_cast<DiagnosticPluginInterface *>(instance);
        if (!plugin) {
            loader->unload();
            loader->deleteLater();
            continue;
        }

        const QString id = plugin->pluginId();
        if (id.isEmpty() || m_plugins.contains(id)) {
            loader->unload();
            loader->deleteLater();
            continue;
        }

        m_plugins.insert(id, plugin);
        m_loaders.push_back(loader);
    }

    if (m_plugins.isEmpty() && error) {
        *error = QStringLiteral("no valid diagnostic plugins found");
    }
    return !m_plugins.isEmpty();
}

QVector<QString> DiagnosticPluginManager::pluginIds() const
{
    return m_plugins.keys().toVector();
}

DiagnosticPluginInterface *DiagnosticPluginManager::plugin(const QString &pluginId) const
{
    return m_plugins.value(pluginId, nullptr);
}

DiagnosticPluginInterface *DiagnosticPluginManager::pluginForComponent(const QString &componentType) const
{
    const QString key = componentType.trimmed();
    for (auto *plugin : m_plugins) {
        if (plugin && plugin->componentType().trimmed() == key) {
            return plugin;
        }
    }
    return nullptr;
}

void DiagnosticPluginManager::clearLoaders()
{
    for (auto *loader : m_loaders) {
        if (loader) {
            loader->unload();
            loader->deleteLater();
        }
    }
    m_loaders.clear();
}
