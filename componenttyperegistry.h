#ifndef COMPONENTTYPEREGISTRY_H
#define COMPONENTTYPEREGISTRY_H

#include <QMap>
#include <QString>
#include <QStringList>

struct ComponentTypeRegistryData
{
    QStringList types;
    QMap<QString, QString> bindings;
};

class ComponentTypeRegistry
{
public:
    static QString registryPath();
    static QString normalizeTypeKey(const QString &value);
    static QStringList defaultTypes();
    static QStringList defaultTypeKeys();
    static bool isBuiltInType(const QString &typeName);

    static ComponentTypeRegistryData load();
    static bool save(const ComponentTypeRegistryData &data, QString *errorMessage = nullptr);

    static QString typeNameFromClassId(int cls, const ComponentTypeRegistryData &data);
    static int classIdFromTypeName(const QString &typeName, const ComponentTypeRegistryData &data);
    static QString resolvePluginForType(const QString &typeName, const ComponentTypeRegistryData &data);
};

#endif // COMPONENTTYPEREGISTRY_H
