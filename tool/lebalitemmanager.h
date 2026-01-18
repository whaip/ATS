#ifndef LEBALITEMMANAGER_H
#define LEBALITEMMANAGER_H

#include <QGraphicsScene>
#include <QListWidgetItem>
#include <QWidget>
#include <vector>

#include "../ComponentsDetect/componenttypes.h"

namespace Ui {
class LebalItemManager;
}

class LabelEditingWindow;

class LebalItemManager : public QWidget
{
    Q_OBJECT

public:
    explicit LebalItemManager(QWidget *parent = nullptr);
    ~LebalItemManager();

    void setImage(const QImage &image);
    void setLabels(const QList<CompLabel> &labels, bool clearExisting = true);
    bool centerZoomToComponent(const QString &componentId);
    bool centerZoomToLabelId(int labelId);

signals:
    void diagnoseRequested(const QList<CompLabel> &labels);

private slots:
    void importLabels();
    void exportLabels();
    void addLabel();
    void deleteSelected();
    void diagnoseSelected();
    void handleSceneSelectionChanged();
    void handleListSelectionChanged();
    void handleListItemActivated(QListWidgetItem *item);
    void showContextMenu(const QPoint &pos);

private:
    void emitDiagnoseAll();

    Ui::LebalItemManager *ui;
    bool m_syncingSelection = false;
    LabelEditingWindow *m_labelEditingWindow = nullptr;
    QImage m_currentImage;
    std::vector<int> m_deleteIds;
};

#endif // LEBALITEMMANAGER_H
