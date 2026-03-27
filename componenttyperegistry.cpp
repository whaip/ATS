#include "componenttyperegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>

namespace {
QStringList candidateRegistryPaths()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
    candidates << QDir(appDir).filePath(QStringLiteral("board_db/component_type_registry.json"));
    candidates << QDir(appDir).filePath(QStringLiteral("build/release/board_db/component_type_registry.json"));
    candidates << QDir(appDir).filePath(QStringLiteral("build/debug/board_db/component_type_registry.json"));

    QDir probe(appDir);
    for (int depth = 0; depth < 8; ++depth) {
        candidates << probe.filePath(QStringLiteral("board_db/component_type_registry.json"));
        candidates << probe.filePath(QStringLiteral("build/release/board_db/component_type_registry.json"));
        candidates << probe.filePath(QStringLiteral("build/debug/board_db/component_type_registry.json"));
        if (!probe.cdUp()) {
            break;
        }
    }

    return candidates;
}

QStringList candidateLegacyBindingPaths()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
    candidates << QDir(appDir).filePath(QStringLiteral("board_db/component_plugin_bindings.json"));
    candidates << QDir(appDir).filePath(QStringLiteral("build/release/board_db/component_plugin_bindings.json"));
    candidates << QDir(appDir).filePath(QStringLiteral("build/debug/board_db/component_plugin_bindings.json"));

    QDir probe(appDir);
    for (int depth = 0; depth < 8; ++depth) {
        candidates << probe.filePath(QStringLiteral("board_db/component_plugin_bindings.json"));
        candidates << probe.filePath(QStringLiteral("build/release/board_db/component_plugin_bindings.json"));
        candidates << probe.filePath(QStringLiteral("build/debug/board_db/component_plugin_bindings.json"));
        if (!probe.cdUp()) {
            break;
        }
    }
    return candidates;
}
}

QString ComponentTypeRegistry::registryPath()
{
    const QStringList candidates = candidateRegistryPaths();
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            return QDir::cleanPath(path);
        }
    }
    return QDir::cleanPath(candidates.first());
}

QString ComponentTypeRegistry::normalizeTypeKey(const QString &value)
{
    QString out = value.trimmed().toLower();
    out.remove(QRegularExpression(QStringLiteral("\\s+")));
    out.remove(QStringLiteral("_"));
    out.remove(QStringLiteral("-"));
    return out;
}

QStringList ComponentTypeRegistry::defaultTypes()
{
    return {
        QStringLiteral("Capacitor"),
        QStringLiteral("IC"),
        QStringLiteral("LED"),
        QStringLiteral("Resistor"),
        QStringLiteral("battery"),
        QStringLiteral("buzzer"),
        QStringLiteral("clock"),
        QStringLiteral("connector"),
        QStringLiteral("diode"),
        QStringLiteral("display"),
        QStringLiteral("fuse"),
        QStringLiteral("inductor"),
        QStringLiteral("potentiometer"),
        QStringLiteral("relay"),
        QStringLiteral("switch"),
        QStringLiteral("transistor")
    };
}

QStringList ComponentTypeRegistry::defaultTypeKeys()
{
    QStringList keys;
    const QStringList types = defaultTypes();
    keys.reserve(types.size());
    for (const QString &typeName : types) {
        const QString key = normalizeTypeKey(typeName);
        if (!key.isEmpty() && !keys.contains(key)) {
            keys.push_back(key);
        }
    }
    return keys;
}

bool ComponentTypeRegistry::isBuiltInType(const QString &typeName)
{
    const QString key = normalizeTypeKey(typeName);
    if (key.isEmpty()) {
        return false;
    }
    return defaultTypeKeys().contains(key);
}

ComponentTypeRegistryData ComponentTypeRegistry::load()
{
    ComponentTypeRegistryData data;
    data.types = defaultTypes();

    QJsonObject root;
    QFile file(registryPath());
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            root = doc.object();
        }
    }
    if (root.isEmpty()) {
        for (const QString &legacyPath : candidateLegacyBindingPaths()) {
            QFile legacy(legacyPath);
            if (!legacy.exists() || !legacy.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            QJsonParseError legacyError;
            const QJsonDocument legacyDoc = QJsonDocument::fromJson(legacy.readAll(), &legacyError);
            if (legacyError.error != QJsonParseError::NoError || !legacyDoc.isObject()) {
                continue;
            }
            root = legacyDoc.object();
            break;
        }
        if (root.isEmpty()) {
            return data;
        }
    }
    const QJsonArray types = root.value(QStringLiteral("types")).toArray();
    QStringList loadedTypes;
    for (const QJsonValue &value : types) {
        const QString typeName = value.toString().trimmed();
        if (!typeName.isEmpty()) {
            loadedTypes.push_back(typeName);
        }
    }
    if (!loadedTypes.isEmpty()) {
        data.types = loadedTypes;
    }

    const QJsonArray bindings = root.value(QStringLiteral("bindings")).toArray();
    for (const QJsonValue &value : bindings) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject item = value.toObject();
        const QString typeKey = normalizeTypeKey(item.value(QStringLiteral("componentType")).toString());
        const QString pluginId = item.value(QStringLiteral("pluginId")).toString().trimmed();
        if (!typeKey.isEmpty() && !pluginId.isEmpty()) {
            data.bindings.insert(typeKey, pluginId);
        }
    }

    return data;
}

bool ComponentTypeRegistry::save(const ComponentTypeRegistryData &data, QString *errorMessage)
{
    QJsonArray types;
    QStringList uniqueTypes;
    for (const QString &raw : data.types) {
        const QString name = raw.trimmed();
        if (!name.isEmpty() && !uniqueTypes.contains(name, Qt::CaseInsensitive)) {
            uniqueTypes.push_back(name);
            types.append(name);
        }
    }

    QJsonArray bindings;
    for (const QString &typeName : uniqueTypes) {
        const QString key = normalizeTypeKey(typeName);
        const QString pluginId = data.bindings.value(key).trimmed();
        if (pluginId.isEmpty()) {
            continue;
        }
        QJsonObject item;
        item.insert(QStringLiteral("componentType"), typeName);
        item.insert(QStringLiteral("pluginId"), pluginId);
        bindings.append(item);
    }

    QJsonObject root;
    root.insert(QStringLiteral("types"), types);
    root.insert(QStringLiteral("bindings"), bindings);

    const QString path = registryPath();
    QFileInfo info(path);
    QDir dir = info.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot create directory: %1").arg(dir.absolutePath());
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot open file for write: %1").arg(path);
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to save file: %1").arg(path);
        }
        return false;
    }
    return true;
}

QString ComponentTypeRegistry::typeNameFromClassId(int cls, const ComponentTypeRegistryData &data)
{
    if (cls >= 0 && cls < data.types.size()) {
        return data.types.at(cls);
    }
    if (!data.types.isEmpty()) {
        return data.types.first();
    }
    return QStringLiteral("Unknown");
}

int ComponentTypeRegistry::classIdFromTypeName(const QString &typeName, const ComponentTypeRegistryData &data)
{
    const QString normalized = typeName.trimmed();
    for (int i = 0; i < data.types.size(); ++i) {
        if (QString::compare(data.types.at(i), normalized, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return 0;
}

QString ComponentTypeRegistry::resolvePluginForType(const QString &typeName, const ComponentTypeRegistryData &data)
{
    return data.bindings.value(normalizeTypeKey(typeName)).trimmed();
}
