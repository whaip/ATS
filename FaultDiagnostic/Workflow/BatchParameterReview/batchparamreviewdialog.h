#ifndef BATCHPARAMREVIEWDIALOG_H
#define BATCHPARAMREVIEWDIALOG_H

#include <QDialog>
#include <QMap>
#include <QVariant>
#include <QVector>

#include "../../TPS/Core/tpsmodels.h"

class QListWidget;
class QTableWidget;
class QWidget;

struct BatchParamReviewItem {
    QString componentRef;
    QString pluginId;
    QVector<TPSParamDefinition> definitions;
    QMap<QString, QVariant> values;
};

class BatchParamReviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BatchParamReviewDialog(QWidget *parent = nullptr);

    void setItems(const QVector<BatchParamReviewItem> &items);
    QVector<BatchParamReviewItem> reviewedItems() const;

private slots:
    void onCurrentTaskChanged(int row);

protected:
    void accept() override;

private:
    QWidget *createEditor(const TPSParamDefinition &definition, const QVariant &value, QWidget *parent = nullptr) const;
    QVariant readEditorValue(const TPSParamDefinition &definition, QWidget *editor) const;
    void saveCurrentTaskValues();
    void loadTaskToTable(int index);

    QListWidget *m_taskList = nullptr;
    QTableWidget *m_paramTable = nullptr;
    QVector<BatchParamReviewItem> m_items;
    QVector<QWidget *> m_currentEditors;
    int m_currentTaskIndex = -1;
};

#endif // BATCHPARAMREVIEWDIALOG_H
