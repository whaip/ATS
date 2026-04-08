#ifndef RUNTIMERECORDSPAGE_H
#define RUNTIMERECORDSPAGE_H

#include <QDate>
#include <QVector>
#include <QWidget>

namespace Ui {
class RuntimeRecordsPage;
}

class RuntimeRecordsPage : public QWidget
{
    Q_OBJECT

public:
    explicit RuntimeRecordsPage(QWidget *parent = nullptr);
    ~RuntimeRecordsPage() override;

    void refresh();

protected:
    void changeEvent(QEvent *event) override;

private:
    struct RuntimeRecord {
        QString taskId;
        QString runId;
        QString boardId;
        QString componentRef;
        QString pluginId;
        QString createdAt;
        QString closedAt;
        QString taskFilePath;
        QString logDirPath;
        QString logFilePath;
        bool taskExists = false;
        bool logExists = false;
        QDate createdDate;
    };

    QString resolveRuntimeLogsPath() const;
    QString resolveRuntimeTasksPath() const;
    void applyTheme();
    void setupTable();
    void loadRecords();
    void rebuildTable();
    void updateDetailViews();
    void updateSummaryLabel();
    QVector<int> filteredRecordIndexes() const;
    QString readTextFile(const QString &path) const;
    bool removeRecordArtifacts(const RuntimeRecord &record, QString *errorMessage) const;
    bool pathWithinRoot(const QString &path, const QString &rootPath) const;

    Ui::RuntimeRecordsPage *ui = nullptr;
    QVector<RuntimeRecord> m_records;
    QVector<int> m_visibleIndexes;
};

#endif // RUNTIMERECORDSPAGE_H
