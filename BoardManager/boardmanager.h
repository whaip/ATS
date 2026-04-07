#ifndef BOARDMANAGER_H
#define BOARDMANAGER_H

#include <QImage>
#include <QProcess>
#include <QString>
#include <QVector>
#include <QWidget>
#include <QMap>
#include "../ComponentsDetect/componenttypes.h"

class BoardsRepository;

namespace Ui {
class BoardManager;
}

class QLabel;
class QPushButton;
class QResizeEvent;
class LebalItemManager;

class BoardManager : public QWidget
{
    Q_OBJECT

public:
    explicit BoardManager(QWidget *parent = nullptr);
    ~BoardManager();

signals:
    void testRequested(const QString &boardId,
                       const QList<CompLabel> &selectedLabels,
                       const QList<CompLabel> &allLabels,
                       const QImage &boardImage);

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onSearchBoards();
    void onNewBoard();
    void onAutoRecognizeBoard();
    void onDeleteBoard();
    void onRefreshBoards();
    void onStartSelectedTest();
    void onBoardSelectionChanged();
    void onLabelsChanged(const QList<CompLabel> &labels);
    void onTrainEmbeddingModel();
    void onEmbeddingTrainingOutput();
    void onEmbeddingTrainingFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    struct BoardEntry {
        QString boardId;
        QString name;
        QString model;
        QString version;
        QString created;
        QString sourcePath;
        QVector<CompLabel> components;
    };

    QString resolveDatabasePath() const;
    QString resolveImagePath(const QString &imagePath) const;
    QString resolveEmbeddingModelPath() const;
    QString resolveEmbeddingTrainingScriptPath() const;
    void setupUiElements();
    void setupConnections();
    void populateBoardTable(const QString &filterText = QString());
    int currentRepositoryIndex() const;
    void updateInfoPanel(const BoardEntry &entry);
    void resetInfoPanel();
    void loadBoardsFromStorage();
    void persistBoards();
    BoardEntry buildEntryFromRepositoryBoard(int index) const;
    bool captureImageFromCamera(QImage *capturedImage);
    bool extractRoiForRecognition(const QImage &sourceImage,
                                  QImage *roiImage,
                                  bool *roiIsManualCrop = nullptr);
    void selectBoardByRepositoryIndex(int repositoryIndex);

    Ui::BoardManager *ui;
    LebalItemManager *m_labelEditor = nullptr;
    QVector<BoardEntry> m_boards;
    QVector<int> m_rowToRepositoryIndex;
    BoardsRepository *m_repository = nullptr;
    bool m_loadingBoard = false;
    QString m_databasePath;
    QPushButton *m_trainEmbeddingButton = nullptr;
    QProcess *m_embeddingTrainingProcess = nullptr;
};

#endif // BOARDMANAGER_H
