#include "boardrepository.h"
#include "componenttyperegistry.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

namespace {
QStringList componentTypeNames()
{
    return ComponentTypeRegistry::load().types;
}

QString typeNameFromClassId(int cls)
{
    ComponentTypeRegistryData data;
    data.types = componentTypeNames();
    return ComponentTypeRegistry::typeNameFromClassId(cls, data);
}

int classIdFromTypeName(const QString &typeName)
{
    ComponentTypeRegistryData data;
    data.types = componentTypeNames();
    return ComponentTypeRegistry::classIdFromTypeName(typeName, data);
}

QByteArray decodeNotesField(const QJsonValue &value)
{
    if (!value.isString()) {
        return {};
    }

    const QByteArray raw = value.toString().toUtf8();
    const QByteArray decoded = QByteArray::fromBase64(raw);
    if (!decoded.isEmpty() || raw.isEmpty()) {
        return decoded;
    }
    return raw;
}

QString encodeNotesField(const QByteArray &notes)
{
    return QString::fromUtf8(notes.toBase64());
}
}

QList<CompLabel> BoardRecord::toLabels() const
{
    QList<CompLabel> labels;
    labels.reserve(components.size());
    for (const BoardComponentRecord &component : components) {
        labels.append(component.label);
    }
    return labels;
}

BoardsRepository::BoardsRepository(QObject *parent)
    : QObject(parent)
{
}

bool BoardsRepository::load(const QString &filePath, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = tr("无法打开数据库文件: %1").arg(filePath);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = tr("JSON 解析失败: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonArray boardArray = root.value(QStringLiteral("boards")).toArray();

    QVector<BoardRecord> boards;
    boards.reserve(boardArray.size());
    for (const QJsonValue &value : boardArray) {
        if (value.isObject()) {
            boards.append(boardFromJson(value.toObject()));
        }
    }

    m_databasePath = filePath;
    m_rootExtra = root;
    m_rootExtra.remove(QStringLiteral("boards"));
    m_boards = boards;
    emit boardsChanged();
    return true;
}

bool BoardsRepository::save(QString *errorMessage) const
{
    if (m_databasePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("数据库路径为空，无法保存。");
        }
        return false;
    }

    QJsonObject root = m_rootExtra;
    QJsonArray boardArray;
    for (const BoardRecord &board : m_boards) {
        boardArray.append(boardToJson(board));
    }
    root.insert(QStringLiteral("boards"), boardArray);

    QSaveFile file(m_databasePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = tr("无法写入数据库文件: %1").arg(m_databasePath);
        }
        return false;
    }

    const QJsonDocument document(root);
    file.write(document.toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = tr("数据库写回失败: %1").arg(m_databasePath);
        }
        return false;
    }

    return true;
}

QString BoardsRepository::databasePath() const
{
    return m_databasePath;
}

QVector<BoardRecord> BoardsRepository::boards() const
{
    return m_boards;
}

int BoardsRepository::boardCount() const
{
    return m_boards.size();
}

const BoardRecord *BoardsRepository::boardAt(int index) const
{
    if (index < 0 || index >= m_boards.size()) {
        return nullptr;
    }
    return &m_boards.at(index);
}

int BoardsRepository::indexOfBoard(const QString &boardId) const
{
    for (int index = 0; index < m_boards.size(); ++index) {
        if (m_boards.at(index).boardId == boardId) {
            return index;
        }
    }
    return -1;
}

bool BoardsRepository::addBoard(const BoardRecord &board, QString *errorMessage)
{
    if (board.boardId.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("boardId 不能为空。");
        }
        return false;
    }

    if (isBoardIdDuplicate(m_boards, board.boardId)) {
        if (errorMessage) {
            *errorMessage = tr("boardId 已存在: %1").arg(board.boardId);
        }
        return false;
    }

    m_boards.push_back(board);
    emit boardsChanged();
    return true;
}

bool BoardsRepository::updateBoard(int index, const BoardRecord &board, QString *errorMessage)
{
    if (index < 0 || index >= m_boards.size()) {
        if (errorMessage) {
            *errorMessage = tr("无效的板卡索引。");
        }
        return false;
    }

    if (board.boardId.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("boardId 不能为空。");
        }
        return false;
    }

    if (isBoardIdDuplicate(m_boards, board.boardId, index)) {
        if (errorMessage) {
            *errorMessage = tr("boardId 已存在: %1").arg(board.boardId);
        }
        return false;
    }

    m_boards[index] = board;
    emit boardsChanged();
    return true;
}

bool BoardsRepository::removeBoardAt(int index, QString *errorMessage)
{
    if (index < 0 || index >= m_boards.size()) {
        if (errorMessage) {
            *errorMessage = tr("无效的板卡索引。");
        }
        return false;
    }

    m_boards.removeAt(index);
    emit boardsChanged();
    return true;
}

bool BoardsRepository::setBoardComponents(int index, const QList<CompLabel> &labels, QString *errorMessage)
{
    if (index < 0 || index >= m_boards.size()) {
        if (errorMessage) {
            *errorMessage = tr("无效的板卡索引。");
        }
        return false;
    }

    BoardRecord board = m_boards.at(index);
    QList<BoardComponentRecord> merged;
    merged.reserve(labels.size());

    for (const CompLabel &label : labels) {
        BoardComponentRecord component;
        const auto existingIt = std::find_if(board.components.cbegin(), board.components.cend(),
            [&label](const BoardComponentRecord &record) {
                return record.label.id == label.id;
            });

        if (existingIt != board.components.cend()) {
            component = *existingIt;
        }

        component.label = label;
        component.typeName = typeNameFromClassId(label.cls);
        if (!component.model.isEmpty() || !label.label.isEmpty()) {
            component.model = label.label;
        }
        merged.append(component);
    }

    board.components = merged;

    int maxId = 0;
    for (const CompLabel &label : labels) {
        maxId = std::max(maxId, label.id);
    }
    board.nextComponentIndex = std::max(board.nextComponentIndex, maxId + 1);

    return updateBoard(index, board, errorMessage);
}

BoardRecord BoardsRepository::boardFromJson(const QJsonObject &object)
{
    BoardRecord board;
    board.boardId = object.value(QStringLiteral("boardId")).toString();
    board.name = object.value(QStringLiteral("name")).toString();
    board.version = object.value(QStringLiteral("version")).toString(QStringLiteral("v1"));
    board.imagePath = object.value(QStringLiteral("imagePath")).toString();
    board.createdAt = object.value(QStringLiteral("createdAt")).toString();
    board.nextComponentIndex = object.value(QStringLiteral("nextComponentIndex")).toInt(1);

    board.extraFields = object;
    board.extraFields.remove(QStringLiteral("boardId"));
    board.extraFields.remove(QStringLiteral("name"));
    board.extraFields.remove(QStringLiteral("version"));
    board.extraFields.remove(QStringLiteral("imagePath"));
    board.extraFields.remove(QStringLiteral("createdAt"));
    board.extraFields.remove(QStringLiteral("nextComponentIndex"));
    board.extraFields.remove(QStringLiteral("components"));

    const QJsonArray components = object.value(QStringLiteral("components")).toArray();
    for (const QJsonValue &value : components) {
        if (value.isObject()) {
            board.components.append(componentFromJson(value.toObject()));
        }
    }

    if (board.createdAt.isEmpty()) {
        board.createdAt = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    return board;
}

QJsonObject BoardsRepository::boardToJson(const BoardRecord &board)
{
    QJsonObject object = board.extraFields;
    object.insert(QStringLiteral("boardId"), board.boardId);
    object.insert(QStringLiteral("name"), board.name);
    object.insert(QStringLiteral("version"), board.version);
    object.insert(QStringLiteral("imagePath"), board.imagePath);
    object.insert(QStringLiteral("createdAt"), board.createdAt);
    object.insert(QStringLiteral("nextComponentIndex"), board.nextComponentIndex);

    QJsonArray components;
    for (const BoardComponentRecord &component : board.components) {
        components.append(componentToJson(component));
    }
    object.insert(QStringLiteral("components"), components);
    return object;
}

BoardComponentRecord BoardsRepository::componentFromJson(const QJsonObject &object)
{
    BoardComponentRecord component;

    const QJsonObject bbox = object.value(QStringLiteral("bbox")).toObject();
    const int id = object.value(QStringLiteral("id")).toInt();
    const double x = bbox.value(QStringLiteral("x")).toDouble();
    const double y = bbox.value(QStringLiteral("y")).toDouble();
    const double w = bbox.value(QStringLiteral("w")).toDouble(1.0);
    const double h = bbox.value(QStringLiteral("h")).toDouble(1.0);
    const QString typeName = object.value(QStringLiteral("type")).toString();
    const double confidence = object.value(QStringLiteral("confidence")).toDouble(100.0);
    const QString labelText = object.value(QStringLiteral("label")).toString(object.value(QStringLiteral("model")).toString());
    const QString positionNumber = object.value(QStringLiteral("reference")).toString();
    const QByteArray notes = decodeNotesField(object.value(QStringLiteral("notes")));

    component.label = CompLabel(id, x, y, w, h, classIdFromTypeName(typeName), confidence, labelText, positionNumber, notes);
    component.typeName = typeName.isEmpty() ? typeNameFromClassId(component.label.cls) : typeName;
    component.model = object.value(QStringLiteral("model")).toString(labelText);
    component.extraFields = object;
    component.extraFields.remove(QStringLiteral("bbox"));
    component.extraFields.remove(QStringLiteral("id"));
    component.extraFields.remove(QStringLiteral("model"));
    component.extraFields.remove(QStringLiteral("label"));
    component.extraFields.remove(QStringLiteral("reference"));
    component.extraFields.remove(QStringLiteral("type"));
    component.extraFields.remove(QStringLiteral("confidence"));
    component.extraFields.remove(QStringLiteral("notes"));
    return component;
}

QJsonObject BoardsRepository::componentToJson(const BoardComponentRecord &component)
{
    QJsonObject object = component.extraFields;
    object.insert(QStringLiteral("id"), component.label.id);

    QJsonObject bbox;
    bbox.insert(QStringLiteral("x"), component.label.x);
    bbox.insert(QStringLiteral("y"), component.label.y);
    bbox.insert(QStringLiteral("w"), component.label.w);
    bbox.insert(QStringLiteral("h"), component.label.h);
    object.insert(QStringLiteral("bbox"), bbox);

    const QString typeName = component.typeName.isEmpty() ? typeNameFromClassId(component.label.cls) : component.typeName;
    object.insert(QStringLiteral("type"), typeName);
    object.insert(QStringLiteral("model"), component.label.label);
    object.insert(QStringLiteral("label"), component.label.label);
    object.insert(QStringLiteral("reference"), component.label.position_number);
    object.insert(QStringLiteral("confidence"), component.label.confidence);
    if (!component.label.notes.isEmpty()) {
        object.insert(QStringLiteral("notes"), encodeNotesField(component.label.notes));
    } else {
        object.remove(QStringLiteral("notes"));
    }

    return object;
}

bool BoardsRepository::isBoardIdDuplicate(const QVector<BoardRecord> &boards, const QString &boardId, int skipIndex)
{
    const QString normalized = boardId.trimmed();
    for (int index = 0; index < boards.size(); ++index) {
        if (index == skipIndex) {
            continue;
        }
        if (QString::compare(boards.at(index).boardId.trimmed(), normalized, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}
