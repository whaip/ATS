#ifndef PORTALLOCATIONREVIEWDIALOG_H
#define PORTALLOCATIONREVIEWDIALOG_H

#include <QDialog>
#include <QVector>

#include "../../TPS/Core/tpsmodels.h"

namespace Ui {
class PortAllocationReviewDialog;
}

struct TaskPortAllocationReviewItem {
    QString componentRef;
    QVector<TPSPortBinding> bindings;
};

class PortAllocationReviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PortAllocationReviewDialog(QWidget *parent = nullptr);
    ~PortAllocationReviewDialog() override;
    void setItems(const QVector<TaskPortAllocationReviewItem> &items);
    QVector<TaskPortAllocationReviewItem> reviewedItems() const;

protected:
    void accept() override;

private:
    struct CandidatePortEntry {
        TPSPortBinding binding;
        bool assigned = false;
        int selectedIndex = -1;
    };

    struct SelectedBindingEntry {
        int taskIndex = -1;
        int bindingIndex = -1;
        QString componentRef;
        TPSPortBinding binding;
        bool assigned = false;
    };

    QVector<TPSPortBinding> candidatesForBinding(const TPSPortBinding &binding) const;
    QString formatPortCandidate(const TPSPortBinding &binding) const;
    void assignCandidateToSelected(int candidateIndex, int selectedIndex);
    void unassignSelected(int selectedIndex);
    int firstUnassignedRowByType(TPSPortType type) const;
    void refreshAvailableTable();
    void refreshSelectedTable();
    void rebuildSummary();

    Ui::PortAllocationReviewDialog *ui = nullptr;
    QVector<TaskPortAllocationReviewItem> m_items;
    QVector<CandidatePortEntry> m_availablePorts;
    QVector<SelectedBindingEntry> m_selectedRows;
};

#endif // PORTALLOCATIONREVIEWDIALOG_H
