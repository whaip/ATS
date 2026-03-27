#include "tpsparamservice.h"
#include "componenttyperegistry.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace {
QString stripQuotes(const QString &value)
{
    QString text = value.trimmed();
    if (text.startsWith('"') && text.endsWith('"') && text.size() >= 2) {
        text = text.mid(1, text.size() - 2);
    }
    return text;
}

QString normalized(const QString &text)
{
    QString value = text.trimmed().toLower();
    value.remove(' ');
    value.remove('_');
    value.remove('-');
    return value;
}

QStringList matchWordsForType(const QString &componentType)
{
    const QString type = normalized(componentType);
    QSet<QString> words;
    if (!type.isEmpty()) {
        words.insert(type);
    }

    const QString splitSource = componentType.toLower();
    const QStringList rawTokens = splitSource.split(QRegularExpression(QStringLiteral("[^a-z0-9\\u4e00-\\u9fa5]+")),
                                                    Qt::SkipEmptyParts);
    for (const QString &token : rawTokens) {
        const QString key = normalized(token);
        if (key.size() >= 2) {
            words.insert(key);
        }
    }

    if (type.contains(QStringLiteral("resistor")) || type.contains(QStringLiteral("resistance")) || type.contains(QStringLiteral("res"))) {
        words.insert(QStringLiteral("resistor"));
        words.insert(QStringLiteral("resistance"));
        words.insert(QStringLiteral("res"));
    }
    if (type.contains(QStringLiteral("capacitor")) || type.contains(QStringLiteral("capacitance")) || type.contains(QStringLiteral("cap"))) {
        words.insert(QStringLiteral("capacitor"));
        words.insert(QStringLiteral("capacitance"));
        words.insert(QStringLiteral("cap"));
    }
    if (type.contains(QStringLiteral("transistor")) || type.contains(QStringLiteral("trans"))) {
        words.insert(QStringLiteral("transistor"));
        words.insert(QStringLiteral("trans"));
    }
    if (type.contains(QStringLiteral("diode")) || type.contains(QStringLiteral("diod"))) {
        words.insert(QStringLiteral("diode"));
        words.insert(QStringLiteral("diod"));
    }

    return QStringList(words.begin(), words.end());
}

QVariant parseExpressionLite(const QString &expression)
{
    const QString expr = expression.trimmed();

    QRegularExpression stringLiteralRe(QStringLiteral(R"___(QStringLiteral\("([^"]*)"\))___"));
    QRegularExpressionMatch m = stringLiteralRe.match(expr);
    if (m.hasMatch()) {
        return m.captured(1);
    }

    if ((expr.startsWith('"') && expr.endsWith('"')) || (expr.startsWith('\'') && expr.endsWith('\''))) {
        return stripQuotes(expr);
    }

    if (expr.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (expr.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
        return false;
    }

    bool intOk = false;
    const int intValue = expr.toInt(&intOk);
    if (intOk) {
        return intValue;
    }

    bool doubleOk = false;
    const double doubleValue = expr.toDouble(&doubleOk);
    if (doubleOk) {
        return doubleValue;
    }

    return expr;
}

QVector<TpsParamDefinitionLite> parseParamsFromBlock(const QString &body)
{
    QVector<TpsParamDefinitionLite> params;
    if (body.trimmed().isEmpty()) {
        return params;
    }

    QMap<QString, TpsParamDefinitionLite> byVar;
    QRegularExpression declRe(QStringLiteral(R"(TPSParamDefinition\s+(\w+)\s*;)") );
    auto declIt = declRe.globalMatch(body);
    while (declIt.hasNext()) {
        const auto match = declIt.next();
        byVar.insert(match.captured(1), TpsParamDefinitionLite());
    }

    QRegularExpression assignRe(QStringLiteral(R"((\w+)\.(key|label|type|defaultValue|minValue|maxValue|stepValue)\s*=\s*(.+?);)") );
    auto assignIt = assignRe.globalMatch(body);
    while (assignIt.hasNext()) {
        const auto match = assignIt.next();
        const QString var = match.captured(1);
        const QString field = match.captured(2);
        const QString expr = match.captured(3).trimmed();
        if (!byVar.contains(var)) {
            continue;
        }

        TpsParamDefinitionLite def = byVar.value(var);
        if (field == QStringLiteral("key")) {
            def.key = parseExpressionLite(expr).toString();
        } else if (field == QStringLiteral("label")) {
            def.label = parseExpressionLite(expr).toString();
        } else if (field == QStringLiteral("type")) {
            QRegularExpression typeRe(QStringLiteral(R"(TPSParamType::(\w+))"));
            QRegularExpressionMatch typeMatch = typeRe.match(expr);
            def.type = typeMatch.hasMatch() ? typeMatch.captured(1) : expr;
        } else if (field == QStringLiteral("defaultValue")) {
            def.defaultValue = parseExpressionLite(expr);
        } else if (field == QStringLiteral("minValue")) {
            def.minValue = parseExpressionLite(expr);
        } else if (field == QStringLiteral("maxValue")) {
            def.maxValue = parseExpressionLite(expr);
        } else if (field == QStringLiteral("stepValue")) {
            def.stepValue = parseExpressionLite(expr);
        }
        byVar.insert(var, def);
    }

    QRegularExpression enumRe(QStringLiteral(R"((\w+)\.enumOptions\s*=\s*\{([^}]*)\}\s*;)") );
    auto enumIt = enumRe.globalMatch(body);
    while (enumIt.hasNext()) {
        const auto match = enumIt.next();
        const QString var = match.captured(1);
        if (!byVar.contains(var)) {
            continue;
        }
        TpsParamDefinitionLite def = byVar.value(var);
        const QString inside = match.captured(2);
        const QStringList raw = inside.split(',', Qt::SkipEmptyParts);
        QStringList options;
        for (const QString &token : raw) {
            options.push_back(parseExpressionLite(token.trimmed()).toString());
        }
        def.enumOptions = options;
        byVar.insert(var, def);
    }

    for (auto it = byVar.begin(); it != byVar.end(); ++it) {
        if (!it.value().key.isEmpty()) {
            if (it.value().label.isEmpty()) {
                it.value().label = it.value().key;
            }
            if (it.value().type.isEmpty()) {
                it.value().type = QStringLiteral("String");
            }
            params.push_back(it.value());
        }
    }

    std::sort(params.begin(), params.end(), [](const TpsParamDefinitionLite &a, const TpsParamDefinitionLite &b) {
        return a.key < b.key;
    });

    return params;
}
}

void TpsParamService::setPluginSourceDir(const QString &dirPath)
{
    m_pluginSourceDir = dirPath;
}

QString TpsParamService::pluginSourceDir() const
{
    return m_pluginSourceDir;
}

void TpsParamService::setDatabasePath(const QString &filePath)
{
    m_databasePath = filePath;
}

QString TpsParamService::databasePath() const
{
    return m_databasePath;
}

bool TpsParamService::loadPlugins(QString *errorMessage)
{
    m_plugins.clear();

    QDir dir(m_pluginSourceDir);
    if (!dir.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("TPS鎻掍欢鐩綍涓嶅瓨鍦? %1").arg(m_pluginSourceDir);
        }
        return false;
    }

    const QStringList files = dir.entryList({QStringLiteral("*tpsplugin.cpp")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QString &fileName : files) {
        const QString filePath = dir.filePath(fileName);
        const TpsPluginSpecLite spec = parsePluginFile(filePath);
        if (!spec.pluginId.isEmpty() || !spec.parameters.isEmpty()) {
            m_plugins.push_back(spec);
        }
    }

    if (m_plugins.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("鏈湪鐩綍涓彂鐜板彲瑙ｆ瀽鐨凾PS鎻掍欢: %1").arg(m_pluginSourceDir);
        }
        return false;
    }

    return true;
}

QVector<TpsPluginSpecLite> TpsParamService::plugins() const
{
    return m_plugins;
}

TpsPluginSpecLite TpsParamService::pluginForComponentType(const QString &componentType) const
{
    const ComponentTypeRegistryData registry = ComponentTypeRegistry::load();
    const QString boundPlugin = ComponentTypeRegistry::resolvePluginForType(componentType, registry);
    if (boundPlugin.isEmpty()) {
        return {};
    }
    for (const TpsPluginSpecLite &plugin : m_plugins) {
        if (plugin.pluginId == boundPlugin) {
            return plugin;
        }
    }

    return {};
}

QMap<QString, QVariant> TpsParamService::loadComponentParams(const QString &boardId,
                                                             int componentId,
                                                             const QString &pluginId) const
{
    QMap<QString, QVariant> params;
    QFile file(m_databasePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return params;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return params;
    }

    const QJsonArray items = doc.object().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("boardId")).toString() == boardId
            && object.value(QStringLiteral("componentId")).toInt() == componentId) {
            const QString pid = object.value(QStringLiteral("pluginId")).toString();
            if (!pluginId.isEmpty() && !pid.isEmpty() && pid != pluginId) {
                continue;
            }
            const QJsonObject paramObj = object.value(QStringLiteral("parameters")).toObject();
            for (auto it = paramObj.begin(); it != paramObj.end(); ++it) {
                params.insert(it.key(), it.value().toVariant());
            }
            break;
        }
    }

    return params;
}

bool TpsParamService::saveComponentParams(const QString &boardId,
                                          int componentId,
                                          const QString &pluginId,
                                          const QString &componentType,
                                          const QMap<QString, QVariant> &parameters,
                                          QString *errorMessage) const
{
    QJsonObject root;
    QJsonArray items;

    if (m_databasePath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Database path is empty.");
        }
        return false;
    }

    QFile file(m_databasePath);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("鏃犳硶璇诲彇鍙傛暟鏁版嵁搴? %1 (%2)")
                                    .arg(m_databasePath, file.errorString());
            }
            return false;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            root = doc.object();
            items = root.value(QStringLiteral("items")).toArray();
        }

        file.close();
    }

    QJsonObject entry;
    bool found = false;
    for (int index = 0; index < items.size(); ++index) {
        if (!items.at(index).isObject()) {
            continue;
        }
        const QJsonObject object = items.at(index).toObject();
        if (object.value(QStringLiteral("boardId")).toString() == boardId
            && object.value(QStringLiteral("componentId")).toInt() == componentId) {
            entry = object;
            found = true;
            break;
        }
    }

    entry.insert(QStringLiteral("boardId"), boardId);
    entry.insert(QStringLiteral("componentId"), componentId);
    entry.insert(QStringLiteral("pluginId"), pluginId);
    entry.insert(QStringLiteral("componentType"), componentType);
    entry.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));

    QJsonObject paramObject;
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
        paramObject.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    entry.insert(QStringLiteral("parameters"), paramObject);

    if (found) {
        for (int index = 0; index < items.size(); ++index) {
            if (!items.at(index).isObject()) {
                continue;
            }
            const QJsonObject object = items.at(index).toObject();
            if (object.value(QStringLiteral("boardId")).toString() == boardId
                && object.value(QStringLiteral("componentId")).toInt() == componentId) {
                items[index] = entry;
                break;
            }
        }
    } else {
        items.append(entry);
    }

    root.insert(QStringLiteral("items"), items);

    QFileInfo dbInfo(m_databasePath);
    QDir dbDir = dbInfo.dir();
    if (!dbDir.exists() && !dbDir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("鏃犳硶鍒涘缓鍙傛暟鏁版嵁搴撶洰褰? %1").arg(dbDir.absolutePath());
        }
        return false;
    }

    QSaveFile saveFile(m_databasePath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("鏃犳硶鍐欏叆鍙傛暟鏁版嵁搴? %1 (%2)")
                                .arg(m_databasePath, saveFile.errorString());
        }
        return false;
    }

    const QJsonDocument output(root);
    const QByteArray payload = output.toJson(QJsonDocument::Indented);
    const qint64 written = saveFile.write(payload);
    if (written != payload.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("鍙傛暟鏁版嵁搴撳啓鍏ヤ笉瀹屾暣: %1 (%2)")
                                .arg(m_databasePath, saveFile.errorString());
        }
        saveFile.cancelWriting();
        return false;
    }

    if (!saveFile.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("鍙傛暟鏁版嵁搴撲繚瀛樺け璐? %1 (%2)")
                                .arg(m_databasePath, saveFile.errorString());
        }
        return false;
    }

    return true;
}

bool TpsParamService::removeBoardParams(const QString &boardId, QString *errorMessage) const
{
    if (m_databasePath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Database path is empty.");
        }
        return false;
    }

    QFile file(m_databasePath);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("鏃犳硶璇诲彇鍙傛暟鏁版嵁搴? %1 (%2)")
                                .arg(m_databasePath, file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("鍙傛暟鏁版嵁搴撴牸寮忔棤鏁? %1").arg(m_databasePath);
        }
        return false;
    }

    QJsonObject root = doc.object();
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    QJsonArray keptItems;

    const QString normalizedBoardId = boardId.trimmed();
    bool removed = false;
    for (const QJsonValue &value : items) {
        if (!value.isObject()) {
            keptItems.append(value);
            continue;
        }

        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("boardId")).toString().trimmed() == normalizedBoardId) {
            removed = true;
            continue;
        }
        keptItems.append(object);
    }

    if (!removed) {
        return true;
    }

    root.insert(QStringLiteral("items"), keptItems);

    QFileInfo dbInfo(m_databasePath);
    QDir dbDir = dbInfo.dir();
    if (!dbDir.exists() && !dbDir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("鏃犳硶鍒涘缓鍙傛暟鏁版嵁搴撶洰褰? %1").arg(dbDir.absolutePath());
        }
        return false;
    }

    QSaveFile saveFile(m_databasePath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("鏃犳硶鍐欏叆鍙傛暟鏁版嵁搴? %1 (%2)")
                                .arg(m_databasePath, saveFile.errorString());
        }
        return false;
    }

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    const qint64 written = saveFile.write(payload);
    if (written != payload.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("鍙傛暟鏁版嵁搴撳啓鍏ヤ笉瀹屾暣: %1 (%2)")
                                .arg(m_databasePath, saveFile.errorString());
        }
        saveFile.cancelWriting();
        return false;
    }

    if (!saveFile.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("鍙傛暟鏁版嵁搴撲繚瀛樺け璐? %1 (%2)")
                                .arg(m_databasePath, saveFile.errorString());
        }
        return false;
    }

    return true;
}

TpsPluginSpecLite TpsParamService::parsePluginFile(const QString &filePath)
{
    TpsPluginSpecLite spec;
    spec.sourceFile = QFileInfo(filePath).fileName();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return spec;
    }

    const QString content = QString::fromUtf8(file.readAll());
    spec.pluginId = parsePluginId(content);
    spec.displayName = parseDisplayName(content);
    spec.parameters = parseRequirementsParams(content);

    if (spec.pluginId.isEmpty()) {
        QString base = QFileInfo(filePath).baseName().toLower();
        base.remove(QStringLiteral("tpsplugin"));
        spec.pluginId = QStringLiteral("tps.%1").arg(base);
    }
    if (spec.displayName.isEmpty()) {
        spec.displayName = spec.pluginId;
    }

    return spec;
}

QString TpsParamService::parsePluginId(const QString &content)
{
    QRegularExpression re(QStringLiteral(R"___(pluginId\s*\(\)\s*const\s*\{[\s\S]*?return\s+QStringLiteral\("([^"]+)"\))___"));
    QRegularExpressionMatch match = re.match(content);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    QRegularExpression helperCallRe(QStringLiteral(R"___(pluginId\s*\(\)\s*const\s*\{[\s\S]*?return\s+([A-Za-z_]\w*)\s*\(\s*\)\s*;)___"));
    QRegularExpressionMatch helperCallMatch = helperCallRe.match(content);
    if (helperCallMatch.hasMatch()) {
        const QString helperName = helperCallMatch.captured(1);
        const QRegularExpression helperRe(QStringLiteral(R"___(QString\s+%1\s*\([^\)]*\)\s*\{[\s\S]*?return\s+QStringLiteral\("([^"]+)"\))___")
                                              .arg(QRegularExpression::escape(helperName)));
        QRegularExpressionMatch helperMatch = helperRe.match(content);
        if (helperMatch.hasMatch()) {
            return helperMatch.captured(1);
        }
    }

    return {};
}

QString TpsParamService::parseDisplayName(const QString &content)
{
    QRegularExpression re(QStringLiteral(R"___(displayName\s*\(\)\s*const\s*\{[\s\S]*?return\s+QStringLiteral\("([^"]+)"\))___"));
    QRegularExpressionMatch match = re.match(content);
    if (match.hasMatch()) {
        return match.captured(1);
    }

    QRegularExpression helperCallRe(QStringLiteral(R"___(displayName\s*\(\)\s*const\s*\{[\s\S]*?return\s+([A-Za-z_]\w*)\s*\(\s*\)\s*;)___"));
    QRegularExpressionMatch helperCallMatch = helperCallRe.match(content);
    if (helperCallMatch.hasMatch()) {
        const QString helperName = helperCallMatch.captured(1);
        const QRegularExpression helperRe(QStringLiteral(R"___(QString\s+%1\s*\([^\)]*\)\s*\{[\s\S]*?return\s+QStringLiteral\("([^"]+)"\))___")
                                              .arg(QRegularExpression::escape(helperName)));
        QRegularExpressionMatch helperMatch = helperRe.match(content);
        if (helperMatch.hasMatch()) {
            return helperMatch.captured(1);
        }
    }

    return {};
}

QVector<TpsParamDefinitionLite> TpsParamService::parseRequirementsParams(const QString &content)
{
    QVector<TpsParamDefinitionLite> params = parseParamsFromBlock(extractFunctionBody(content, QStringLiteral("requirements")));
    if (!params.isEmpty()) {
        return params;
    }

    params = parseParamsFromBlock(extractFunctionBody(content, QStringLiteral("parameterDefinitions")));
    if (!params.isEmpty()) {
        return params;
    }

    return parseParamsFromBlock(content);
}

QString TpsParamService::extractFunctionBody(const QString &content, const QString &functionName)
{
    const int signatureIndex = content.indexOf(QRegularExpression(QStringLiteral(R"(%1\s*\(\)\s*const)").arg(QRegularExpression::escape(functionName))));
    if (signatureIndex < 0) {
        return {};
    }

    const int openBraceIndex = content.indexOf('{', signatureIndex);
    if (openBraceIndex < 0) {
        return {};
    }

    int depth = 0;
    for (int index = openBraceIndex; index < content.size(); ++index) {
        const QChar ch = content.at(index);
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return content.mid(openBraceIndex + 1, index - openBraceIndex - 1);
            }
        }
    }

    return {};
}

QVariant TpsParamService::parseExpressionValue(const QString &expression)
{
    const QString expr = expression.trimmed();

    QRegularExpression stringLiteralRe(QStringLiteral(R"___(QStringLiteral\("([^"]*)"\))___"));
    QRegularExpressionMatch m = stringLiteralRe.match(expr);
    if (m.hasMatch()) {
        return m.captured(1);
    }

    if ((expr.startsWith('"') && expr.endsWith('"')) || (expr.startsWith('\'') && expr.endsWith('\''))) {
        return stripQuotes(expr);
    }

    if (expr.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (expr.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
        return false;
    }

    bool intOk = false;
    const int intValue = expr.toInt(&intOk);
    if (intOk) {
        return intValue;
    }

    bool doubleOk = false;
    const double doubleValue = expr.toDouble(&doubleOk);
    if (doubleOk) {
        return doubleValue;
    }

    return expr;
}

QString TpsParamService::normalizedComponentType(const QString &text)
{
    return normalized(text);
}
