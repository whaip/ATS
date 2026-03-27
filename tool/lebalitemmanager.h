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
class TpsParamService;

class LebalItemManager : public QWidget
{
    Q_OBJECT

public:
    explicit LebalItemManager(QWidget *parent = nullptr);
    ~LebalItemManager();

    void setImage(const QImage &image);
    void setLabels(const QList<CompLabel> &labels, bool clearExisting = true);
    QList<CompLabel> currentLabels() const;
    QList<CompLabel> selectedLabels() const;
    bool centerZoomToComponent(const QString &componentId);
    bool centerZoomToLabelId(int labelId);
    void setCurrentBoardId(const QString &boardId);
    void setTpsParameterDatabasePath(const QString &databasePath);

signals:
    void diagnoseRequested(const QList<CompLabel> &labels);
    void labelsChanged(const QList<CompLabel> &labels);
    void selectedLabelsChanged(const QList<CompLabel> &labels);

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
    void emitLabelsChanged();
    void emitSelectedLabelsChanged();
    bool ensureTpsParamServiceLoaded();
    void showTpsParameterDialog(const CompLabel &label, const QList<int> &anchorIds);

    Ui::LebalItemManager *ui;
    bool m_syncingSelection = false;
    LabelEditingWindow *m_labelEditingWindow = nullptr;
    QImage m_currentImage;
    std::vector<int> m_deleteIds;
    QString m_currentBoardId;
    QString m_tpsParameterDbPath;
    QString m_pluginSourceDir;
    TpsParamService *m_tpsParamService = nullptr;
};

#endif // LEBALITEMMANAGER_H
