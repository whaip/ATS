#include "tpspluginmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QPluginLoader>

TPSPluginManager::TPSPluginManager(QObject *parent)
    : QObject(parent)
{
}

TPSPluginManager::~TPSPluginManager()
{
    clearLoaders();
}

void TPSPluginManager::setPluginDir(const QString &path)
{
    m_pluginDir = path;
}

QString TPSPluginManager::pluginDir() const
{
    return m_pluginDir;
}

void TPSPluginManager::addBuiltin(TPSPluginInterface *plugin)
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

bool TPSPluginManager::loadAll(QString *error)
{
    clearLoaders();
    m_plugins = m_builtins;

    QStringList candidateDirs;
    if (!m_pluginDir.trimmed().isEmpty()) {
        candidateDirs << QDir::cleanPath(m_pluginDir);
    } else {
        const QDir appDir(QCoreApplication::applicationDirPath());
        candidateDirs << appDir.filePath(QStringLiteral("tps_plugins"));
        candidateDirs << appDir.filePath(QStringLiteral("plugins/tps"));
    }

    bool anyDirExists = false;
    for (const QString &baseDir : candidateDirs) {
        const QDir dir(baseDir);
        if (!dir.exists()) {
            continue;
        }
        anyDirExists = true;

        const QStringList entries = dir.entryList(QDir::Files);
        for (const auto &fileName : entries) {
            const QString path = dir.filePath(fileName);
            auto *loader = new QPluginLoader(path, this);
            QObject *instance = loader->instance();
            if (!instance) {
                loader->deleteLater();
                continue;
            }
            auto *plugin = qobject_cast<TPSPluginInterface *>(instance);
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
    }

    if (m_plugins.isEmpty()) {
        if (error) {
            if (!anyDirExists && !candidateDirs.isEmpty()) {
                *error = QStringLiteral("plugin directory not found: %1").arg(candidateDirs.first());
            } else {
                *error = QStringLiteral("no valid TPS plugins found");
            }
        }
        return false;
    }
    return true;
}

QVector<QString> TPSPluginManager::pluginIds() const
{
    return m_plugins.keys().toVector();
}

TPSPluginInterface *TPSPluginManager::plugin(const QString &pluginId) const
{
    return m_plugins.value(pluginId, nullptr);
}

void TPSPluginManager::clearLoaders()
{
    for (auto *loader : m_loaders) {
        if (loader) {
            loader->unload();
            loader->deleteLater();
        }
    }
    m_loaders.clear();
}
