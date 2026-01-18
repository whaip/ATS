#ifndef BOARDMANAGER_H
#define BOARDMANAGER_H

#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>
#include "../ComponentsDetect/componenttypes.h"
#include "boarddatamanager.h"

namespace Ui {
class BoardManager;
}

class QLabel;
class QResizeEvent;
class LebalItemManager;

class BoardManager : public QWidget
{
    Q_OBJECT

public:
    explicit BoardManager(QWidget *parent = nullptr);
    ~BoardManager();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onSearchBoards();
    void onNewBoard();
    void onDeleteBoard();
    void onRefreshBoards();
    void onOpenLabelEditor();
    void onBoardSelectionChanged();

private:
    struct BoardEntry {
        QString name;
        QString model;
        QString version;
        QString created;
        QString sourcePath;
        QImage sourceImage;
        QVector<CompLabel> components;
    };

    void setupUiElements();
    void setupConnections();
    void populateBoardTable(const QString &filterText = QString());
    int findBoardByName(const QString &name) const;
    void updateInfoPanel(const BoardEntry &entry);
    void resetInfoPanel();
    void loadBoardsFromStorage();
    void persistBoards();

    Ui::BoardManager *ui;
    LebalItemManager *m_labelEditor = nullptr;
    QVector<BoardEntry> m_boards;
    BoardDataManager m_dataManager;
};

#endif // BOARDMANAGER_H
