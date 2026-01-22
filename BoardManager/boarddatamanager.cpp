#include "boarddatamanager.h"

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

QString anchorsFileName()
{
    return QStringLiteral("anchors.json");
}

QString planBindingsFileName()
{
    return QStringLiteral("plan_bindings.json");
}

QString legacyPlansFileName()
{
    return QStringLiteral("plans.json");
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

QJsonObject pointToJson(const QPointF &point)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("x"), point.x());
    obj.insert(QStringLiteral("y"), point.y());
    return obj;
}

QPointF pointFromJson(const QJsonObject &obj)
{
    return QPointF(obj.value(QStringLiteral("x")).toDouble(),
                   obj.value(QStringLiteral("y")).toDouble());
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
    m_anchors.clear();
    m_planBindings.clear();

    if (m_storageDir.isEmpty()) {
        return false;
    }

    const QDir dir(m_storageDir);
    const QString boardsPath = dir.filePath(boardsFileName());
    const QString anchorsPath = dir.filePath(anchorsFileName());
    const QString bindingsPath = dir.filePath(planBindingsFileName());
    const QString legacyPlansPath = dir.filePath(legacyPlansFileName());

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

    if (QFile::exists(anchorsPath)) {
        QFile file(anchorsPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            const QJsonObject root = doc.object();
            const QJsonArray list = root.value(QStringLiteral("anchors")).toArray();
            for (const auto &value : list) {
                m_anchors.push_back(anchorFromJson(value.toObject()));
            }
        }
    }

    if (QFile::exists(bindingsPath)) {
        QFile file(bindingsPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            const QJsonObject root = doc.object();
            const QJsonArray list = root.value(QStringLiteral("planBindings")).toArray();
            for (const auto &value : list) {
                m_planBindings.push_back(planBindingFromJson(value.toObject()));
            }
        }
    }

    if (QFile::exists(legacyPlansPath)) {
        QFile file(legacyPlansPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            const QJsonObject root = doc.object();
            const QJsonArray list = root.value(QStringLiteral("plans")).toArray();
            for (const auto &value : list) {
                const QJsonObject planObj = value.toObject();
                const QString boardName = planObj.value(QStringLiteral("boardName")).toString();
                const int boardIndex = findBoardIndex(boardName);
                if (boardIndex < 0) {
                    continue;
                }
                BoardInfoRecord &board = m_boards[boardIndex];
                if (!board.components.isEmpty()) {
                    continue;
                }

                const QJsonArray components = planObj.value(QStringLiteral("components")).toArray();
                for (const auto &compValue : components) {
                    const QJsonObject compObj = compValue.toObject();
                    BoardComponentRecord record;
                    record.reference = compObj.value(QStringLiteral("id")).toString();
                    const QJsonObject params = compObj.value(QStringLiteral("params")).toObject();
                    record.type = params.value(QStringLiteral("label")).toString(record.reference);
                    record.model = QString();
                    QJsonObject rectObj = compObj.value(QStringLiteral("location")).toObject();
                    if (rectObj.isEmpty()) {
                        rectObj = compObj.value(QStringLiteral("wiring")).toObject();
                    }
                    record.bbox = rectFromJson(rectObj);
                    board.components.push_back(record);
                }
                board.nextComponentIndex = computeNextComponentIndex(board.components);
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
    const QString anchorsPath = dir.filePath(anchorsFileName());
    const QString bindingsPath = dir.filePath(planBindingsFileName());

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

    QJsonArray anchorsArray;
    for (const auto &anchor : m_anchors) {
        anchorsArray.append(anchorToJson(anchor));
    }
    QJsonObject anchorsRoot;
    anchorsRoot.insert(QStringLiteral("anchors"), anchorsArray);

    QFile anchorsFile(anchorsPath);
    if (!anchorsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    anchorsFile.write(QJsonDocument(anchorsRoot).toJson(QJsonDocument::Indented));

    QJsonArray bindingsArray;
    for (const auto &binding : m_planBindings) {
        bindingsArray.append(planBindingToJson(binding));
    }
    QJsonObject bindingsRoot;
    bindingsRoot.insert(QStringLiteral("planBindings"), bindingsArray);

    QFile bindingsFile(bindingsPath);
    if (!bindingsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    bindingsFile.write(QJsonDocument(bindingsRoot).toJson(QJsonDocument::Indented));

    return true;
}

const QVector<BoardInfoRecord> &BoardDataManager::boards() const
{
    return m_boards;
}

const QVector<BoardAnchorRecord> &BoardDataManager::anchors() const
{
    return m_anchors;
}

const QVector<BoardPlanBindingRecord> &BoardDataManager::planBindings() const
{
    return m_planBindings;
}

bool BoardDataManager::addBoard(const BoardInfoRecord &board)
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

bool BoardDataManager::updateBoard(const BoardInfoRecord &board)
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

bool BoardDataManager::removeBoard(const QString &boardName)
{
    const int index = findBoardIndex(boardName);
    if (index < 0) {
        return false;
    }
    m_boards.removeAt(index);

    m_anchors.erase(std::remove_if(m_anchors.begin(), m_anchors.end(),
                                   [&boardName](const BoardAnchorRecord &record) {
                                       return normalizedName(record.boardName) == normalizedName(boardName);
                                   }),
                    m_anchors.end());

    m_planBindings.erase(std::remove_if(m_planBindings.begin(), m_planBindings.end(),
                                        [&boardName](const BoardPlanBindingRecord &record) {
                                            return normalizedName(record.boardName) == normalizedName(boardName);
                                        }),
                         m_planBindings.end());
    return true;
}

QString BoardDataManager::normalizeComponentReference(const QString &boardName, const QString &suggestedRef)
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

bool BoardDataManager::upsertComponent(const QString &boardName, const BoardComponentRecord &component)
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

bool BoardDataManager::removeComponent(const QString &boardName, const QString &componentRef)
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

    const int anchorIndex = findAnchorIndex(boardName, componentRef);
    if (anchorIndex >= 0) {
        m_anchors.removeAt(anchorIndex);
    }

    const int bindingIndex = findPlanBindingIndex(boardName);
    if (bindingIndex >= 0) {
        auto &record = m_planBindings[bindingIndex];
        record.bindings.erase(std::remove_if(record.bindings.begin(), record.bindings.end(),
                                             [&componentRef](const ComponentPlanBinding &binding) {
                                                 return normalizedName(binding.componentRef) == normalizedName(componentRef);
                                             }),
                              record.bindings.end());
    }

    return true;
}

bool BoardDataManager::upsertAnchors(const QString &boardName, const QString &componentRef, const QVector<AnchorPoint> &anchors)
{
    if (boardName.trimmed().isEmpty() || componentRef.trimmed().isEmpty()) {
        return false;
    }

    const int index = findAnchorIndex(boardName, componentRef);
    if (index >= 0) {
        m_anchors[index].anchors = anchors;
        return true;
    }

    BoardAnchorRecord record;
    record.boardName = boardName;
    record.componentRef = componentRef;
    record.anchors = anchors;
    m_anchors.push_back(record);
    return true;
}

bool BoardDataManager::removeAnchors(const QString &boardName, const QString &componentRef)
{
    const int index = findAnchorIndex(boardName, componentRef);
    if (index < 0) {
        return false;
    }
    m_anchors.removeAt(index);
    return true;
}

void BoardDataManager::clearAnchors(const QString &boardName)
{
    m_anchors.erase(std::remove_if(m_anchors.begin(), m_anchors.end(),
                                   [&boardName](const BoardAnchorRecord &record) {
                                       return normalizedName(record.boardName) == normalizedName(boardName);
                                   }),
                    m_anchors.end());
}

bool BoardDataManager::upsertPlanBinding(const QString &boardName, const ComponentPlanBinding &binding)
{
    if (boardName.trimmed().isEmpty() || binding.componentRef.trimmed().isEmpty()) {
        return false;
    }

    int index = findPlanBindingIndex(boardName);
    if (index < 0) {
        BoardPlanBindingRecord record;
        record.boardName = boardName;
        record.bindings.push_back(binding);
        m_planBindings.push_back(record);
        return true;
    }

    auto &record = m_planBindings[index];
    for (auto &item : record.bindings) {
        if (normalizedName(item.componentRef) == normalizedName(binding.componentRef)
            && normalizedName(item.planId) == normalizedName(binding.planId)) {
            item = binding;
            return true;
        }
    }

    record.bindings.push_back(binding);
    return true;
}

bool BoardDataManager::removePlanBinding(const QString &boardName, const QString &componentRef, const QString &planId)
{
    const int index = findPlanBindingIndex(boardName);
    if (index < 0) {
        return false;
    }

    auto &record = m_planBindings[index];
    record.bindings.erase(std::remove_if(record.bindings.begin(), record.bindings.end(),
                                         [&componentRef, &planId](const ComponentPlanBinding &binding) {
                                             return normalizedName(binding.componentRef) == normalizedName(componentRef)
                                                 && normalizedName(binding.planId) == normalizedName(planId);
                                         }),
                          record.bindings.end());
    return true;
}

void BoardDataManager::clearPlanBindings(const QString &boardName)
{
    const int index = findPlanBindingIndex(boardName);
    if (index < 0) {
        return;
    }
    m_planBindings[index].bindings.clear();
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

int BoardDataManager::findAnchorIndex(const QString &boardName, const QString &componentRef) const
{
    const QString boardKey = normalizedName(boardName);
    const QString componentKey = normalizedName(componentRef);
    for (int i = 0; i < m_anchors.size(); ++i) {
        if (normalizedName(m_anchors[i].boardName) == boardKey
            && normalizedName(m_anchors[i].componentRef) == componentKey) {
            return i;
        }
    }
    return -1;
}

int BoardDataManager::findPlanBindingIndex(const QString &boardName) const
{
    const QString key = normalizedName(boardName);
    for (int i = 0; i < m_planBindings.size(); ++i) {
        if (normalizedName(m_planBindings[i].boardName) == key) {
            return i;
        }
    }
    return -1;
}

QJsonObject BoardDataManager::boardToJson(const BoardInfoRecord &board) const
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

QJsonObject BoardDataManager::componentToJson(const BoardComponentRecord &component) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("reference"), component.reference);
    obj.insert(QStringLiteral("type"), component.type);
    obj.insert(QStringLiteral("model"), component.model);
    obj.insert(QStringLiteral("bbox"), rectToJson(component.bbox));
    return obj;
}

QJsonObject BoardDataManager::anchorToJson(const BoardAnchorRecord &record) const
{
    QJsonArray anchors;
    for (const auto &anchor : record.anchors) {
        QJsonObject anchorObj;
        anchorObj.insert(QStringLiteral("id"), anchor.id);
        anchorObj.insert(QStringLiteral("label"), anchor.label);
        anchorObj.insert(QStringLiteral("position"), rectToJson(anchor.position));
        anchors.append(anchorObj);
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("boardName"), record.boardName);
    obj.insert(QStringLiteral("componentRef"), record.componentRef);
    obj.insert(QStringLiteral("anchors"), anchors);
    return obj;
}

QJsonObject BoardDataManager::planBindingToJson(const BoardPlanBindingRecord &record) const
{
    QJsonArray bindings;
    for (const auto &binding : record.bindings) {
        QJsonObject params;
        for (auto it = binding.parameterValues.cbegin(); it != binding.parameterValues.cend(); ++it) {
            params.insert(it.key(), QJsonValue::fromVariant(it.value()));
        }

        QJsonObject bindingObj;
        bindingObj.insert(QStringLiteral("componentRef"), binding.componentRef);
        bindingObj.insert(QStringLiteral("planId"), binding.planId);
        bindingObj.insert(QStringLiteral("parameterValues"), params);
        bindingObj.insert(QStringLiteral("hasTemperatureOverride"), binding.hasTemperatureOverride);
        bindingObj.insert(QStringLiteral("temperatureOverride"), temperatureSpecToJson(binding.temperatureOverride));
        bindings.append(bindingObj);
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("boardName"), record.boardName);
    obj.insert(QStringLiteral("bindings"), bindings);
    return obj;
}

BoardInfoRecord BoardDataManager::boardFromJson(const QJsonObject &obj) const
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
    return board;
}

BoardComponentRecord BoardDataManager::componentFromJson(const QJsonObject &obj) const
{
    BoardComponentRecord component;
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
    return component;
}

BoardAnchorRecord BoardDataManager::anchorFromJson(const QJsonObject &obj) const
{
    BoardAnchorRecord record;
    record.boardName = obj.value(QStringLiteral("boardName")).toString();
    record.componentRef = obj.value(QStringLiteral("componentRef")).toString();
    const QJsonArray anchors = obj.value(QStringLiteral("anchors")).toArray();
    for (const auto &value : anchors) {
        const QJsonObject anchorObj = value.toObject();
        AnchorPoint anchor;
        anchor.id = anchorObj.value(QStringLiteral("id")).toString();
        anchor.label = anchorObj.value(QStringLiteral("label")).toString();
        anchor.position = rectFromJson(anchorObj.value(QStringLiteral("position")).toObject());
        record.anchors.push_back(anchor);
    }
    return record;
}

BoardPlanBindingRecord BoardDataManager::planBindingFromJson(const QJsonObject &obj) const
{
    BoardPlanBindingRecord record;
    record.boardName = obj.value(QStringLiteral("boardName")).toString();

    const QJsonArray list = obj.value(QStringLiteral("bindings")).toArray();
    for (const auto &value : list) {
        const QJsonObject bindingObj = value.toObject();
        ComponentPlanBinding binding;
        binding.componentRef = bindingObj.value(QStringLiteral("componentRef")).toString();
        binding.planId = bindingObj.value(QStringLiteral("planId")).toString();

        const QJsonObject params = bindingObj.value(QStringLiteral("parameterValues")).toObject();
        for (auto it = params.begin(); it != params.end(); ++it) {
            binding.parameterValues.insert(it.key(), it.value().toVariant());
        }

        binding.hasTemperatureOverride = bindingObj.value(QStringLiteral("hasTemperatureOverride")).toBool(false);
        binding.temperatureOverride = temperatureSpecFromJson(bindingObj.value(QStringLiteral("temperatureOverride")).toObject());
        record.bindings.push_back(binding);
    }
    return record;
}

QJsonObject BoardDataManager::temperatureSpecToJson(const TemperatureSpec &spec) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("monitorPoint"), pointToJson(spec.MonitorPoint));
    obj.insert(QStringLiteral("monitorPosition"), rectToJson(spec.MonitorPosition));
    obj.insert(QStringLiteral("alarmThresholdC"), spec.alarmThresholdC);
    obj.insert(QStringLiteral("needContinuousCapture"), spec.needContinuousCapture);
    obj.insert(QStringLiteral("captureMode"), spec.captureMode);
    return obj;
}

TemperatureSpec BoardDataManager::temperatureSpecFromJson(const QJsonObject &obj) const
{
    TemperatureSpec spec;
    spec.MonitorPoint = pointFromJson(obj.value(QStringLiteral("monitorPoint")).toObject());
    spec.MonitorPosition = rectFromJson(obj.value(QStringLiteral("monitorPosition")).toObject());
    spec.alarmThresholdC = obj.value(QStringLiteral("alarmThresholdC")).toDouble(spec.alarmThresholdC);
    spec.needContinuousCapture = obj.value(QStringLiteral("needContinuousCapture")).toBool(spec.needContinuousCapture);
    spec.captureMode = obj.value(QStringLiteral("captureMode")).toString();
    return spec;
}

int BoardDataManager::computeNextComponentIndex(const QVector<BoardComponentRecord> &components) const
{
    int maxIndex = 0;
    for (const auto &component : components) {
        bool ok = false;
        const int value = component.reference.toInt(&ok);
        if (ok) {
            maxIndex = std::max(maxIndex, value);
        }
    }
    return maxIndex + 1;
}
