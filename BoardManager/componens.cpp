#include "componens.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonObject>
#include <algorithm>

namespace {
QString boardsFileName()
{
    return QStringLiteral("boards.json");
}

QString normalizedName(const QString &name)
{
    return name.trimmed().toLower();
}

QJsonObject rectToJson(const QRectF &rect)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("x"), rect.x());
    obj.insert(QStringLiteral("y"), rect.y());
    obj.insert(QStringLiteral("w"), rect.width());
    obj.insert(QStringLiteral("h"), rect.height());
    return obj;
}

QRectF rectFromJson(const QJsonObject &obj)
{
    return QRectF(obj.value(QStringLiteral("x")).toDouble(),
                  obj.value(QStringLiteral("y")).toDouble(),
                  obj.value(QStringLiteral("w")).toDouble(),
                  obj.value(QStringLiteral("h")).toDouble());
}

}

Componens::Componens(const QString &storageDir)
    : m_storageDir(storageDir)
{
}

void Componens::setStorageDir(const QString &storageDir)
{
    m_storageDir = storageDir;
}

QString Componens::storageDir() const
{
    return m_storageDir;
}

bool Componens::load()
{
    m_boards.clear();

    if (m_storageDir.isEmpty()) {
        return false;
    }

    const QDir dir(m_storageDir);
    const QString boardsPath = dir.filePath(boardsFileName());
    if (QFile::exists(boardsPath)) {
        QFile file(boardsPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            const QJsonObject root = doc.object();
            const QJsonArray list = root.value(QStringLiteral("boards")).toArray();
            for (const auto &value : list) {
                m_boards.push_back(boardFromJson(value.toObject()));
            }
        }
    }

    for (auto &board : m_boards) {
        if (board.nextComponentIndex < 1) {
            board.nextComponentIndex = computeNextComponentIndex(board.components);
        }
    }

    return true;
}

bool Componens::save() const
{
    if (m_storageDir.isEmpty()) {
        return false;
    }

    QDir dir(m_storageDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    const QString boardsPath = dir.filePath(boardsFileName());
    QJsonArray boardsArray;
    for (const auto &board : m_boards) {
        boardsArray.append(boardToJson(board));
    }
    QJsonObject boardsRoot;
    boardsRoot.insert(QStringLiteral("boards"), boardsArray);

    QFile boardsFile(boardsPath);
    if (!boardsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    boardsFile.write(QJsonDocument(boardsRoot).toJson(QJsonDocument::Indented));

    return true;
}

const QVector<BoardInfoRecord> &Componens::boards() const
{
    return m_boards;
}

bool Componens::addBoard(const BoardInfoRecord &board)
{
    if (board.name.trimmed().isEmpty()) {
        return false;
    }
    if (findBoardIndex(board.name) >= 0) {
        return false;
    }

    BoardInfoRecord record = board;
    if (record.nextComponentIndex < 1) {
        record.nextComponentIndex = computeNextComponentIndex(record.components);
    }
    m_boards.push_back(record);
    return true;
}

bool Componens::updateBoard(const BoardInfoRecord &board)
{
    const int index = findBoardIndex(board.name);
    if (index < 0) {
        return false;
    }
    BoardInfoRecord record = board;
    if (record.nextComponentIndex < 1) {
        record.nextComponentIndex = computeNextComponentIndex(record.components);
    }
    m_boards[index] = record;
    return true;
}

bool Componens::removeBoard(const QString &boardName)
{
    const int index = findBoardIndex(boardName);
    if (index < 0) {
        return false;
    }
    m_boards.removeAt(index);

    return true;
}

QString Componens::normalizeComponentReference(const QString &boardName, const QString &suggestedRef)
{
    const int boardIndex = findBoardIndex(boardName);
    if (boardIndex < 0) {
        return suggestedRef.trimmed();
    }

    const QString trimmed = suggestedRef.trimmed();
    if (!trimmed.isEmpty()) {
        return trimmed;
    }

    BoardInfoRecord &board = m_boards[boardIndex];
    int index = board.nextComponentIndex;
    if (index < 1) {
        index = computeNextComponentIndex(board.components);
    }

    auto exists = [&board](const QString &ref) {
        return std::any_of(board.components.begin(), board.components.end(),
                           [&ref](const BoardComponentRecord &item) {
                               return normalizedName(item.reference) == normalizedName(ref);
                           });
    };

    QString candidate = QString::number(index);
    while (exists(candidate)) {
        ++index;
        candidate = QString::number(index);
    }
    board.nextComponentIndex = index + 1;
    return candidate;
}

int Componens::allocateComponentId(const QString &boardName)
{
    const int boardIndex = findBoardIndex(boardName);
    if (boardIndex < 0) {
        return 0;
    }

    BoardInfoRecord &board = m_boards[boardIndex];
    int index = board.nextComponentIndex;
    if (index < 1) {
        index = computeNextComponentIndex(board.components);
    }

    auto exists = [&board](int id) {
        return std::any_of(board.components.begin(), board.components.end(),
                           [id](const BoardComponentRecord &item) {
                               return item.id == id;
                           });
    };

    while (exists(index)) {
        ++index;
    }

    board.nextComponentIndex = index + 1;
    return index;
}

bool Componens::upsertComponent(const QString &boardName, const BoardComponentRecord &component)
{
    const int boardIndex = findBoardIndex(boardName);
    if (boardIndex < 0) {
        return false;
    }

    BoardComponentRecord record = component;
    if (record.reference.trimmed().isEmpty()) {
        record.reference = normalizeComponentReference(boardName, record.reference);
    }
    if (record.reference.trimmed().isEmpty()) {
        return false;
    }

    if (record.id <= 0) {
        record.id = allocateComponentId(boardName);
    }
    if (record.id <= 0) {
        return false;
    }

    BoardInfoRecord &board = m_boards[boardIndex];
    for (auto &item : board.components) {
        if (normalizedName(item.reference) == normalizedName(record.reference)) {
            item = record;
            return true;
        }
    }

    board.components.push_back(record);
    return true;
}

bool Componens::removeComponent(const QString &boardName, const QString &componentRef)
{
    const int boardIndex = findBoardIndex(boardName);
    if (boardIndex < 0) {
        return false;
    }

    BoardInfoRecord &board = m_boards[boardIndex];
    board.components.erase(std::remove_if(board.components.begin(), board.components.end(),
                                          [&componentRef](const BoardComponentRecord &record) {
                                              return normalizedName(record.reference) == normalizedName(componentRef);
                                          }),
                          board.components.end());

    return true;
}

int Componens::findBoardIndex(const QString &boardName) const
{
    const QString key = normalizedName(boardName);
    for (int i = 0; i < m_boards.size(); ++i) {
        if (normalizedName(m_boards[i].name) == key) {
            return i;
        }
    }
    return -1;
}

QJsonObject Componens::boardToJson(const BoardInfoRecord &board) const
{
    QJsonArray components;
    for (const auto &component : board.components) {
        components.append(componentToJson(component));
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("boardId"), board.boardId);
    obj.insert(QStringLiteral("name"), board.name);
    obj.insert(QStringLiteral("version"), board.version);
    obj.insert(QStringLiteral("createdAt"), board.createdAt);
    obj.insert(QStringLiteral("imagePath"), board.imagePath);
    obj.insert(QStringLiteral("nextComponentIndex"), board.nextComponentIndex);
    obj.insert(QStringLiteral("components"), components);
    return obj;
}

QJsonObject Componens::componentToJson(const BoardComponentRecord &component) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), component.id);
    obj.insert(QStringLiteral("reference"), component.reference);
    obj.insert(QStringLiteral("type"), component.type);
    obj.insert(QStringLiteral("model"), component.model);
    obj.insert(QStringLiteral("bbox"), rectToJson(component.bbox));
    return obj;
}

BoardInfoRecord Componens::boardFromJson(const QJsonObject &obj) const
{
    BoardInfoRecord board;
    board.boardId = obj.value(QStringLiteral("boardId")).toString();
    if (board.boardId.isEmpty()) {
        board.boardId = obj.value(QStringLiteral("model")).toString();
    }
    board.name = obj.value(QStringLiteral("name")).toString();
    board.version = obj.value(QStringLiteral("version")).toString();
    board.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    board.imagePath = obj.value(QStringLiteral("imagePath")).toString();
    board.nextComponentIndex = obj.value(QStringLiteral("nextComponentIndex")).toInt(0);

    const QJsonArray components = obj.value(QStringLiteral("components")).toArray();
    for (const auto &value : components) {
        if (value.isObject()) {
            board.components.push_back(componentFromJson(value.toObject()));
        } else if (value.isString()) {
            BoardComponentRecord record;
            record.reference = value.toString();
            board.components.push_back(record);
        }
    }
    if (board.nextComponentIndex < 1) {
        board.nextComponentIndex = computeNextComponentIndex(board.components);
    }
    int nextId = board.nextComponentIndex;
    for (auto &component : board.components) {
        if (component.id <= 0) {
            component.id = nextId++;
        }
    }
    board.nextComponentIndex = nextId;
    return board;
}

BoardComponentRecord Componens::componentFromJson(const QJsonObject &obj) const
{
    BoardComponentRecord component;
    component.id = obj.value(QStringLiteral("id")).toInt(0);
    component.reference = obj.value(QStringLiteral("reference")).toString();
    if (component.reference.isEmpty()) {
        component.reference = obj.value(QStringLiteral("id")).toString();
    }
    component.type = obj.value(QStringLiteral("type")).toString();
    component.model = obj.value(QStringLiteral("model")).toString();
    QJsonObject rectObj = obj.value(QStringLiteral("bbox")).toObject();
    if (rectObj.isEmpty()) {
        rectObj = obj.value(QStringLiteral("location")).toObject();
    }
    if (rectObj.isEmpty()) {
        rectObj = obj.value(QStringLiteral("wiring")).toObject();
    }
    component.bbox = rectFromJson(rectObj);
    if (component.type.isEmpty()) {
        const QJsonObject params = obj.value(QStringLiteral("params")).toObject();
        component.type = params.value(QStringLiteral("label")).toString(component.reference);
    }
    if (component.id <= 0) {
        bool ok = false;
        const int parsed = component.reference.toInt(&ok);
        if (ok) {
            component.id = parsed;
        }
    }
    return component;
}

int Componens::computeNextComponentIndex(const QVector<BoardComponentRecord> &components) const
{
    int maxIndex = 0;
    for (const auto &component : components) {
        maxIndex = std::max(maxIndex, component.id);
    }
    return maxIndex + 1;
}
