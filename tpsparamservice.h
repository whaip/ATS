#ifndef TPSPARAMSERVICE_H
#define TPSPARAMSERVICE_H

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

struct TpsParamDefinitionLite
{
    QString key;
    QString label;
    QString type;
    QVariant defaultValue;
    QVariant minValue;
    QVariant maxValue;
    QVariant stepValue;
    QStringList enumOptions;
};

struct TpsPluginSpecLite
{
    QString pluginId;
    QString displayName;
    QString sourceFile;
    QVector<TpsParamDefinitionLite> parameters;
};

class TpsParamService
{
public:
    void setPluginSourceDir(const QString &dirPath);
    QString pluginSourceDir() const;

    void setDatabasePath(const QString &filePath);
    QString databasePath() const;

    bool loadPlugins(QString *errorMessage = nullptr);
    QVector<TpsPluginSpecLite> plugins() const;
    TpsPluginSpecLite pluginForComponentType(const QString &componentType) const;

    QMap<QString, QVariant> loadComponentParams(const QString &boardId,
                                                int componentId,
                                                const QString &pluginId) const;

    bool saveComponentParams(const QString &boardId,
                             int componentId,
                             const QString &pluginId,
                             const QString &componentType,
                             const QMap<QString, QVariant> &parameters,
                             QString *errorMessage = nullptr) const;

    bool removeBoardParams(const QString &boardId,
                           QString *errorMessage = nullptr) const;

private:
    QString m_pluginSourceDir;
    QString m_databasePath;
    QVector<TpsPluginSpecLite> m_plugins;

    static TpsPluginSpecLite parsePluginFile(const QString &filePath);
    static QString parsePluginId(const QString &content);
    static QString parseDisplayName(const QString &content);
    static QVector<TpsParamDefinitionLite> parseRequirementsParams(const QString &content);
    static QString extractFunctionBody(const QString &content, const QString &functionName);
    static QVariant parseExpressionValue(const QString &expression);
    static QString normalizedComponentType(const QString &text);
};

#endif // TPSPARAMSERVICE_H
