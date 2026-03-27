#include "testsequencemanager.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

TestSequenceManager::TestSequenceManager(QObject *parent)
    : QObject(parent)
{
}

int TestSequenceManager::count() const
{
    return m_items.size();
}

QVector<TestSequenceManager::Item> TestSequenceManager::items() const
{
    return m_items;
}

TestSequenceManager::Item TestSequenceManager::itemAt(int index) const
{
    if (index < 0 || index >= m_items.size()) {
        return {};
    }
    return m_items.at(index);
}

void TestSequenceManager::setItems(const QVector<Item> &items)
{
    m_items = items;
    emit sequenceReset();
}

void TestSequenceManager::addItem(const Item &item)
{
    const int index = m_items.size();
    m_items.push_back(item);
    emit itemAdded(index);
}

void TestSequenceManager::removeItem(int index)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    m_items.removeAt(index);
    emit itemRemoved(index);
}

void TestSequenceManager::updateItem(int index, const Item &item)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    m_items[index] = item;
    emit itemUpdated(index);
}

void TestSequenceManager::updateParameters(int index, const QMap<QString, QVariant> &params)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    m_items[index].parameters = params;
    emit itemUpdated(index);
}

bool TestSequenceManager::loadFromFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("无法打开文件: %1").arg(path);
        }
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        if (error) {
            *error = QStringLiteral("无效的JSON格式");
        }
        return false;
    }

    QVector<Item> loaded;
    const QJsonArray arr = doc.object().value(QStringLiteral("items")).toArray();
    for (const auto &value : arr) {
        if (!value.isObject()) {
            continue;
        }
        loaded.push_back(itemFromJson(value.toObject()));
    }

    setItems(loaded);
    return true;
}

bool TestSequenceManager::saveToFile(const QString &path, QString *error) const
{
    QJsonArray arr;
    for (const auto &item : m_items) {
        arr.append(itemToJson(item));
    }

    QJsonObject root;
    root.insert(QStringLiteral("items"), arr);
    QJsonDocument doc(root);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("无法保存文件: %1").arg(path);
        }
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

TestSequenceManager::Item TestSequenceManager::itemFromJson(const QJsonObject &obj)
{
    Item item;
    item.componentRef = obj.value(QStringLiteral("componentRef")).toString();
    item.componentType = obj.value(QStringLiteral("componentType")).toString();
    item.pluginId = obj.value(QStringLiteral("pluginId")).toString();

    const QJsonObject params = obj.value(QStringLiteral("parameters")).toObject();
    for (auto it = params.begin(); it != params.end(); ++it) {
        item.parameters.insert(it.key(), it.value().toVariant());
    }

    return item;
}

QJsonObject TestSequenceManager::itemToJson(const Item &item)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("componentRef"), item.componentRef);
    obj.insert(QStringLiteral("componentType"), item.componentType);
    obj.insert(QStringLiteral("pluginId"), item.pluginId);

    QJsonObject params;
    for (auto it = item.parameters.begin(); it != item.parameters.end(); ++it) {
        params.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    obj.insert(QStringLiteral("parameters"), params);

    return obj;
}
