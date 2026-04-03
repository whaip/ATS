#ifndef TASKLOGSTATISTICSPAGE_H
#define TASKLOGSTATISTICSPAGE_H

#include <QWidget>

class QSqlTableModel;

namespace Ui {
class TaskLogStatisticsPage;
}

class TaskLogStatisticsPage : public QWidget
{
    Q_OBJECT

public:
    explicit TaskLogStatisticsPage(QWidget *parent = nullptr);
    ~TaskLogStatisticsPage();

    void refresh();

private:
    QString resolveDatabasePath() const;
    bool openDatabase(QString *errorMessage = nullptr);
    void updateStatusLabel(const QString &message, bool isError = false);

    Ui::TaskLogStatisticsPage *ui;
    QString m_connectionName;
    QSqlTableModel *m_model = nullptr;
};

#endif // TASKLOGSTATISTICSPAGE_H
