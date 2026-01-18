#include "boarddatamanager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace {
QString boardsFileName()
{
    return QStringLiteral("boards.json");
}

QString plansFileName()
{
    return QStringLiteral("plans.json");
}

QString normalizedName(const QString &name)
{
    return name.trimmed().toLower();
}
}

BoardDataManager::BoardDataManager(const QString &storageDir)
    : m_storageDir(storageDir)
{
}

void BoardDataManager::setStorageDir(const QString &storageDir)
{
    m_storageDir = storageDir;
}

QString BoardDataManager::storageDir() const
{
    return m_storageDir;
}

bool BoardDataManager::load()
{
    m_boards.clear();
    m_plans.clear();

    if (m_storageDir.isEmpty()) {
        return false;
    }

    const QDir dir(m_storageDir);
    const QString boardsPath = dir.filePath(boardsFileName());
    const QString plansPath = dir.filePath(plansFileName());

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

    if (QFile::exists(plansPath)) {
        QFile file(plansPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            const QJsonObject root = doc.object();
            const QJsonArray list = root.value(QStringLiteral("plans")).toArray();
            for (const auto &value : list) {
                m_plans.push_back(planFromJson(value.toObject()));
            }
        }
    }

    syncPlansToBoards();
    return true;
}

bool BoardDataManager::save() const
{
    if (m_storageDir.isEmpty()) {
        return false;
    }

    QDir dir(m_storageDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    const QString boardsPath = dir.filePath(boardsFileName());
    const QString plansPath = dir.filePath(plansFileName());

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

    QJsonArray plansArray;
    for (const auto &plan : m_plans) {
        plansArray.append(planToJson(plan));
    }
    QJsonObject plansRoot;
    plansRoot.insert(QStringLiteral("plans"), plansArray);

    QFile plansFile(plansPath);
    if (!plansFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    plansFile.write(QJsonDocument(plansRoot).toJson(QJsonDocument::Indented));

    return true;
}

const QVector<BoardInfoRecord> &BoardDataManager::boards() const
{
    return m_boards;
}

const QVector<BoardPlanRecord> &BoardDataManager::plans() const
{
    return m_plans;
}

bool BoardDataManager::addBoard(const BoardInfoRecord &board)
{
    if (board.name.trimmed().isEmpty()) {
        return false;
    }
    if (findBoardIndex(board.name) >= 0) {
        return false;
    }
    m_boards.push_back(board);
    syncPlansToBoards();
    return true;
}

bool BoardDataManager::updateBoard(const BoardInfoRecord &board)
{
    const int index = findBoardIndex(board.name);
    if (index < 0) {
        return false;
    }
    m_boards[index] = board;
    syncPlansToBoards();
    return true;
}

bool BoardDataManager::removeBoard(const QString &boardName)
{
    const int index = findBoardIndex(boardName);
    if (index < 0) {
        return false;
    }
    m_boards.removeAt(index);

    const int planIndex = findPlanIndex(boardName);
    if (planIndex >= 0) {
        m_plans.removeAt(planIndex);
    }
    return true;
}

bool BoardDataManager::upsertComponent(const QString &boardName, const BoardComponentRecord &component)
{
    if (component.id.trimmed().isEmpty()) {
        return false;
    }

    const int boardIndex = findBoardIndex(boardName);
    if (boardIndex < 0) {
        return false;
    }

    BoardInfoRecord &board = m_boards[boardIndex];
    if (!board.componentIds.contains(component.id)) {
        board.componentIds.push_back(component.id);
    }

    const int planIndex = findPlanIndex(boardName);
    if (planIndex < 0) {
        BoardPlanRecord plan;
        plan.boardName = boardName;
        plan.components.push_back(component);
        m_plans.push_back(plan);
        return true;
    }

    auto &plan = m_plans[planIndex];
    bool updated = false;
    for (auto &item : plan.components) {
        if (normalizedName(item.id) == normalizedName(component.id)) {
            item = component;
            updated = true;
            break;
        }
    }
    if (!updated) {
        plan.components.push_back(component);
    }
    return true;
}

bool BoardDataManager::removeComponent(const QString &boardName, const QString &componentId)
{
    const int boardIndex = findBoardIndex(boardName);
    if (boardIndex < 0) {
        return false;
    }

    BoardInfoRecord &board = m_boards[boardIndex];
    board.componentIds.erase(std::remove_if(board.componentIds.begin(), board.componentIds.end(),
                                            [&componentId](const QString &id) {
                                                return normalizedName(id) == normalizedName(componentId);
                                            }),
                              board.componentIds.end());

    const int planIndex = findPlanIndex(boardName);
    if (planIndex >= 0) {
        BoardPlanRecord &plan = m_plans[planIndex];
        plan.components.erase(std::remove_if(plan.components.begin(), plan.components.end(),
                                             [&componentId](const BoardComponentRecord &record) {
                                                 return normalizedName(record.id) == normalizedName(componentId);
                                             }),
                              plan.components.end());
    }

    return true;
}

void BoardDataManager::syncPlansToBoards()
{
    for (const auto &board : m_boards) {
        const int planIndex = findPlanIndex(board.name);
        if (planIndex < 0) {
            BoardPlanRecord plan;
            plan.boardName = board.name;
            for (const auto &id : board.componentIds) {
                BoardComponentRecord record;
                record.id = id;
                plan.components.push_back(record);
            }
            m_plans.push_back(plan);
            continue;
        }

        BoardPlanRecord &plan = m_plans[planIndex];
        QVector<BoardComponentRecord> updatedComponents;
        for (const auto &id : board.componentIds) {
            auto it = std::find_if(plan.components.begin(), plan.components.end(),
                                   [&id](const BoardComponentRecord &record) {
                                       return normalizedName(record.id) == normalizedName(id);
                                   });
            if (it != plan.components.end()) {
                updatedComponents.push_back(*it);
            } else {
                BoardComponentRecord record;
                record.id = id;
                updatedComponents.push_back(record);
            }
        }
        plan.components = updatedComponents;
    }

    m_plans.erase(std::remove_if(m_plans.begin(), m_plans.end(),
                                 [this](const BoardPlanRecord &plan) {
                                     return findBoardIndex(plan.boardName) < 0;
                                 }),
                  m_plans.end());
}

int BoardDataManager::findBoardIndex(const QString &boardName) const
{
    const QString key = normalizedName(boardName);
    for (int i = 0; i < m_boards.size(); ++i) {
        if (normalizedName(m_boards[i].name) == key) {
            return i;
        }
    }
    return -1;
}

int BoardDataManager::findPlanIndex(const QString &boardName) const
{
    const QString key = normalizedName(boardName);
    for (int i = 0; i < m_plans.size(); ++i) {
        if (normalizedName(m_plans[i].boardName) == key) {
            return i;
        }
    }
    return -1;
}

QJsonObject BoardDataManager::boardToJson(const BoardInfoRecord &board) const
{
    QJsonArray components;
    for (const auto &id : board.componentIds) {
        components.append(id);
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("name"), board.name);
    obj.insert(QStringLiteral("model"), board.model);
    obj.insert(QStringLiteral("version"), board.version);
    obj.insert(QStringLiteral("createdAt"), board.createdAt);
    obj.insert(QStringLiteral("imagePath"), board.imagePath);
    obj.insert(QStringLiteral("components"), components);
    return obj;
}

QJsonObject BoardDataManager::componentToJson(const BoardComponentRecord &component) const
{
    QJsonObject rect;
    rect.insert(QStringLiteral("x"), component.wiringRect.x());
    rect.insert(QStringLiteral("y"), component.wiringRect.y());
    rect.insert(QStringLiteral("w"), component.wiringRect.width());
    rect.insert(QStringLiteral("h"), component.wiringRect.height());

    QJsonObject obj;
    obj.insert(QStringLiteral("id"), component.id);
    obj.insert(QStringLiteral("params"), component.params);
    obj.insert(QStringLiteral("location"), rect);
    obj.insert(QStringLiteral("wiring"), rect);
    return obj;
}

QJsonObject BoardDataManager::planToJson(const BoardPlanRecord &plan) const
{
    QJsonArray components;
    for (const auto &component : plan.components) {
        components.append(componentToJson(component));
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("boardName"), plan.boardName);
    obj.insert(QStringLiteral("components"), components);
    return obj;
}

BoardInfoRecord BoardDataManager::boardFromJson(const QJsonObject &obj) const
{
    BoardInfoRecord board;
    board.name = obj.value(QStringLiteral("name")).toString();
    board.model = obj.value(QStringLiteral("model")).toString();
    board.version = obj.value(QStringLiteral("version")).toString();
    board.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    board.imagePath = obj.value(QStringLiteral("imagePath")).toString();

    const QJsonArray components = obj.value(QStringLiteral("components")).toArray();
    for (const auto &value : components) {
        board.componentIds.push_back(value.toString());
    }
    return board;
}

BoardComponentRecord BoardDataManager::componentFromJson(const QJsonObject &obj) const
{
    BoardComponentRecord component;
    component.id = obj.value(QStringLiteral("id")).toString();
    component.params = obj.value(QStringLiteral("params")).toObject();

    QJsonObject rect = obj.value(QStringLiteral("location")).toObject();
    if (rect.isEmpty()) {
        rect = obj.value(QStringLiteral("wiring")).toObject();
    }
    component.wiringRect = QRectF(rect.value(QStringLiteral("x")).toDouble(),
                                  rect.value(QStringLiteral("y")).toDouble(),
                                  rect.value(QStringLiteral("w")).toDouble(),
                                  rect.value(QStringLiteral("h")).toDouble());
    return component;
}

BoardPlanRecord BoardDataManager::planFromJson(const QJsonObject &obj) const
{
    BoardPlanRecord plan;
    plan.boardName = obj.value(QStringLiteral("boardName")).toString();

    const QJsonArray components = obj.value(QStringLiteral("components")).toArray();
    for (const auto &value : components) {
        plan.components.push_back(componentFromJson(value.toObject()));
    }
    return plan;
}
