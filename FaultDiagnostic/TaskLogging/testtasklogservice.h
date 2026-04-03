#ifndef TESTTASKLOGSERVICE_H
#define TESTTASKLOGSERVICE_H

#include <QObject>
#include <QString>

struct TaskContextRecord;

class TestTaskLogService : public QObject
{
    Q_OBJECT

public:
    explicit TestTaskLogService(QObject *parent = nullptr);
    ~TestTaskLogService();

    void setDatabasePath(const QString &filePath);
    QString databasePath() const;

    bool appendTaskRecord(const TaskContextRecord &record, QString *errorMessage = nullptr);

private:
    QString m_databasePath;
    QString m_connectionName;

    QString resolveDatabasePath() const;
    bool ensureDatabase(QString *errorMessage = nullptr);
    bool ensureDirectory(QString *errorMessage = nullptr) const;
};

#endif // TESTTASKLOGSERVICE_H
