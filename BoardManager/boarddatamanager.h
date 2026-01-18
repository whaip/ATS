#ifndef BOARDDATAMANAGER_H
#define BOARDDATAMANAGER_H

#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QVector>

struct BoardComponentRecord {
    QString id;
    QJsonObject params;
    QRectF wiringRect;
};

struct BoardInfoRecord {
    QString name;
    QString model;
    QString version;
    QString createdAt;
    QString imagePath;
    QVector<QString> componentIds;
};

struct BoardPlanRecord {
    QString boardName;
    QVector<BoardComponentRecord> components;
};

class BoardDataManager
{
public:
    explicit BoardDataManager(const QString &storageDir = QString());

    void setStorageDir(const QString &storageDir);
    QString storageDir() const;

    bool load();
    bool save() const;

    const QVector<BoardInfoRecord> &boards() const;
    const QVector<BoardPlanRecord> &plans() const;

    bool addBoard(const BoardInfoRecord &board);
    bool updateBoard(const BoardInfoRecord &board);
    bool removeBoard(const QString &boardName);

    bool upsertComponent(const QString &boardName, const BoardComponentRecord &component);
    bool removeComponent(const QString &boardName, const QString &componentId);

    void syncPlansToBoards();

private:
    int findBoardIndex(const QString &boardName) const;
    int findPlanIndex(const QString &boardName) const;

    QJsonObject boardToJson(const BoardInfoRecord &board) const;
    QJsonObject componentToJson(const BoardComponentRecord &component) const;
    QJsonObject planToJson(const BoardPlanRecord &plan) const;

    BoardInfoRecord boardFromJson(const QJsonObject &obj) const;
    BoardComponentRecord componentFromJson(const QJsonObject &obj) const;
    BoardPlanRecord planFromJson(const QJsonObject &obj) const;

    QString m_storageDir;
    QVector<BoardInfoRecord> m_boards;
    QVector<BoardPlanRecord> m_plans;
};

#endif // BOARDDATAMANAGER_H
