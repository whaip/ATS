#ifndef TESTSEQUENCEMANAGER_H
#define TESTSEQUENCEMANAGER_H

#include <QObject>
#include <QVector>
#include <QMap>
#include <QString>
#include <QVariant>

#include "../TPS/Core/tpsmodels.h"

class TestSequenceManager : public QObject
{
    Q_OBJECT

public:
    struct Item {
        QString componentRef;
        QString pluginId;
        QMap<QString, QVariant> parameters;
    };

    explicit TestSequenceManager(QObject *parent = nullptr);

    int count() const;
    QVector<Item> items() const;
    Item itemAt(int index) const;

    void setItems(const QVector<Item> &items);
    void addItem(const Item &item);
    void removeItem(int index);
    void updateItem(int index, const Item &item);
    void updateParameters(int index, const QMap<QString, QVariant> &params);

    bool loadFromFile(const QString &path, QString *error);
    bool saveToFile(const QString &path, QString *error) const;

signals:
    void sequenceReset();
    void itemAdded(int index);
    void itemRemoved(int index);
    void itemUpdated(int index);

private:
    static Item itemFromJson(const QJsonObject &obj);
    static QJsonObject itemToJson(const Item &item);

    QVector<Item> m_items;
};

#endif // TESTSEQUENCEMANAGER_H
