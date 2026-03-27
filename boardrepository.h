#ifndef BOARDREPOSITORY_H
#define BOARDREPOSITORY_H

#include <QObject>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVector>

#include "./ComponentsDetect/componenttypes.h"

struct BoardComponentRecord
{
    BoardComponentRecord()
        : label(0, 0.0, 0.0, 0.0, 0.0, 0, 0.0, QString(), QString(), QByteArray())
    {
    }

    CompLabel label;
    QString typeName;
    QString model;
    QJsonObject extraFields;
};

struct BoardRecord
{
    QString boardId;
    QString name;
    QString version;
    QString imagePath;
    QString createdAt;
    int nextComponentIndex = 1;
    QList<BoardComponentRecord> components;
    QJsonObject extraFields;

    QList<CompLabel> toLabels() const;
};

class BoardsRepository : public QObject
{
    Q_OBJECT

public:
    explicit BoardsRepository(QObject *parent = nullptr);

    bool load(const QString &filePath, QString *errorMessage = nullptr);
    bool save(QString *errorMessage = nullptr) const;

    QString databasePath() const;
    QVector<BoardRecord> boards() const;
    int boardCount() const;
    const BoardRecord *boardAt(int index) const;
    int indexOfBoard(const QString &boardId) const;

    bool addBoard(const BoardRecord &board, QString *errorMessage = nullptr);
    bool updateBoard(int index, const BoardRecord &board, QString *errorMessage = nullptr);
    bool removeBoardAt(int index, QString *errorMessage = nullptr);
    bool setBoardComponents(int index, const QList<CompLabel> &labels, QString *errorMessage = nullptr);

signals:
    void boardsChanged();

private:
    QString m_databasePath;
    QJsonObject m_rootExtra;
    QVector<BoardRecord> m_boards;

    static BoardRecord boardFromJson(const QJsonObject &object);
    static QJsonObject boardToJson(const BoardRecord &board);
    static BoardComponentRecord componentFromJson(const QJsonObject &object);
    static QJsonObject componentToJson(const BoardComponentRecord &component);
    static bool isBoardIdDuplicate(const QVector<BoardRecord> &boards, const QString &boardId, int skipIndex = -1);
};

#endif // BOARDREPOSITORY_H
