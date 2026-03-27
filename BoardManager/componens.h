#ifndef COMPONENS_H
#define COMPONENS_H

#include <QRectF>
#include <QString>
#include <QVector>
#include "boardprofile.h"

struct BoardComponentRecord {
    int id = 0;
    QString reference;
    QString type;
    QString model;
    QRectF bbox;
};

struct BoardInfoRecord {
    QString boardId;
    QString name;
    QString version;
    QString createdAt;
    QString imagePath;
    int nextComponentIndex = 1;
    QVector<BoardComponentRecord> components;
};

struct BoardAnchorRecord {
    QString boardName;
    QString componentRef;
    QVector<AnchorPoint> anchors;
};

class Componens
{
public:
    explicit Componens(const QString &storageDir = QString());

    void setStorageDir(const QString &storageDir);
    QString storageDir() const;

    bool load();
    bool save() const;

    const QVector<BoardInfoRecord> &boards() const;
    const QVector<BoardAnchorRecord> &anchors() const;
    bool addBoard(const BoardInfoRecord &board);
    bool updateBoard(const BoardInfoRecord &board);
    bool removeBoard(const QString &boardName);

    QString normalizeComponentReference(const QString &boardName, const QString &suggestedRef);
    bool upsertComponent(const QString &boardName, const BoardComponentRecord &component);
    bool removeComponent(const QString &boardName, const QString &componentRef);

    bool upsertAnchors(const QString &boardName, const QString &componentRef, const QVector<AnchorPoint> &anchors);
    bool removeAnchors(const QString &boardName, const QString &componentRef);
    void clearAnchors(const QString &boardName);

    int allocateComponentId(const QString &boardName);

private:
    int findBoardIndex(const QString &boardName) const;
    int findAnchorIndex(const QString &boardName, const QString &componentRef) const;
    QJsonObject boardToJson(const BoardInfoRecord &board) const;
    QJsonObject componentToJson(const BoardComponentRecord &component) const;
    QJsonObject anchorToJson(const BoardAnchorRecord &record) const;
    BoardInfoRecord boardFromJson(const QJsonObject &obj) const;
    BoardComponentRecord componentFromJson(const QJsonObject &obj) const;
    BoardAnchorRecord anchorFromJson(const QJsonObject &obj) const;
    int computeNextComponentIndex(const QVector<BoardComponentRecord> &components) const;

    QString m_storageDir;
    QVector<BoardInfoRecord> m_boards;
    QVector<BoardAnchorRecord> m_anchors;
};
#endif // COMPONENS_H
