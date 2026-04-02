#ifndef LABELEDITING_H
#define LABELEDITING_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QMouseEvent>
#include <QScrollBar>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QList>
#include <QHBoxLayout>
#include <QSplitter>
#include "labelrectitem.h"
#include <QPropertyAnimation>

using Label = CompLabel;


class LabelEditing : public QGraphicsView
{
    Q_OBJECT

public:
    LabelEditing(QWidget *parent, const QImage &image, const std::vector<Label> &label_info, const std::vector<Label> &label_info_add, std::vector<int> &delete_id, QTableWidget*& labelTable);
    ~LabelEditing();

    void loadImage(const QImage &image);
    void setLabels(const std::vector<Label> &labels);
    void getAllLabelItemInfo(std::vector<Label> &result_label_info);
    void setonlyviewmodel();
    void setSelectModel();
    void triggerTableRowClickById(int labelId);
    int getSelectedLabelId() const {
        if (selectRectItem) {
            return std::get<0>(selectRectItem->getItemInfo());
        }
        return -1;  // 杩斿洖-1琛ㄧず娌℃湁閫変腑鐨勯」
    }

    QGraphicsScene* getScene() const { return scene; }

    // 娣诲姞鑾峰彇鎵€鏈夎閫変腑鏍囩淇℃伅鐨勬柟娉?
    std::vector<Label> getSelectedLabelItemInfos() const;

    LabelRectItem* getRectItemById(int id) const;    // 澶栭儴璋冪敤浠ュ埛鏂拌〃鏍煎唴瀹癸紙鍐呴儴璋冪敤 updateLabelTable锛?
    Q_INVOKABLE void refreshTable();
    
    // 瀹屽叏娓呯悊鍜岄噸缃粍浠剁姸鎬侊紙鐢ㄤ簬鏉垮崱鍒囨崲锛?
    void fullReset();

public slots:
    void on_createRectButton_clicked();
    void on_editButton_clicked();
    void on_finishButton_clicked();
    void on_deleteButton_clicked();

signals:
    void window_close();
    void closeRequested();  // 娣诲姞鏂扮殑淇″彿
    void selectionChanged();
    
    // 鏍囩鎿嶄綔鍚屾淇″彿
    void labelAdded(const Label& label);      // 鏍囩娣诲姞淇″彿
    void labelUpdated(const Label& label);    // 鏍囩鏇存柊淇″彿  
    void labelDeleted(int labelId);           // 鏍囩鍒犻櫎淇″彿
    void parameterConfigRequested(const Label& label, const QList<int> &anchorIds);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void clearAllSelection();
    void requestLabelTableRefresh();
    QPushButton *createRectButton;
    QPushButton *editButton;
    QPushButton *finishButton;
    QPushButton *deleteButton;
    QPushButton *colorButton;
    QComboBox *lineWidthComboBox;
    QColor currentColor = Qt::blue;
    qreal currentLineWidth = 2.0;
    QTableWidget*& labelTable;

    void setupLabelTable();
    void updateLabelTable();
    void onTableRowClicked(int row);
    void onTableCellChanged(int row, int column);
    void setLabelTableFocus();
    void selectAndCenterRectItem(int labelId, int row);

    QGraphicsScene *scene;
    LabelRectItem *rubberBand = nullptr;
    QPointF origin;
    std::vector<Label> label_info;
    std::vector<Label> label_info_add;
    std::vector<LabelRectItem*> label_rect_item;
    std::vector<LabelRectItem*> label_rect_item_add;
    std::vector<int> &delete_id;
    bool is_add;
    QImage label_image;
    bool is_editing = false;
    LabelRectItem *createRectItem = nullptr;
    LabelRectItem *selectRectItem = nullptr;
    int current_label_index = -1;
    QString tempLabel;
    bool is_only_view = false;
    bool is_select_model = false;
    QString newLabel;

    void setupUI();
    void updateRectItemStyle(LabelRectItem* item);
    void reloadItems();
    QList<int> collectAllLabelIds() const;
    bool m_tableRefreshPending = false;
};

class LabelEditingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LabelEditingWindow(QWidget *parent,
                                const QImage &image,
                                std::vector<Label> label_info,
                                std::vector<Label> label_info_add,
                                std::vector<int> &delete_id)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

        mainLayout = new QHBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        splitter = new QSplitter(Qt::Horizontal, this);
        splitter->setChildrenCollapsible(false);

        labelTable = new QTableWidget(splitter);
        labelEditing = std::make_shared<LabelEditing>(splitter, image, label_info, label_info_add, delete_id, labelTable);
        connect(labelEditing.get(), &LabelEditing::closeRequested, this, &QWidget::close);
        connect(labelEditing.get(), &LabelEditing::labelAdded, this, &LabelEditingWindow::labelAdded);
        connect(labelEditing.get(), &LabelEditing::labelUpdated, this, &LabelEditingWindow::labelUpdated);
        connect(labelEditing.get(), &LabelEditing::labelDeleted, this, &LabelEditingWindow::labelDeleted);
        connect(labelEditing.get(), &LabelEditing::parameterConfigRequested, this, &LabelEditingWindow::parameterConfigRequested);
        connect(labelEditing.get(), &LabelEditing::selectionChanged, this, &LabelEditingWindow::selectionChanged);

        labelTable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        labelEditing->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

        splitter->addWidget(labelEditing.get());
        splitter->addWidget(labelTable);
        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes(QList<int>{900, 320});

        mainLayout->addWidget(splitter);
        setLayout(mainLayout);
    }

    ~LabelEditingWindow() override = default;

    void setOnlyViewModel()
    {
        labelEditing->setonlyviewmodel();
    }

    void setSelectModel()
    {
        labelEditing->setSelectModel();
    }

    void triggerTableRowClickById(int labelId)
    {
        labelEditing->triggerTableRowClickById(labelId);
    }

    int getSelectedLabelId() const
    {
        return labelEditing->getSelectedLabelId();
    }

    std::shared_ptr<LabelEditing> labelEditing;

signals:
    void window_close();
    void labelAdded(const Label &label);
    void labelUpdated(const Label &label);
    void labelDeleted(int labelId);
    void parameterConfigRequested(const Label &label, const QList<int> &anchorIds);
    void selectionChanged();

protected:
    void closeEvent(QCloseEvent *event) override
    {
        emit window_close();
        event->accept();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);

        if (mainLayout) {
            mainLayout->update();
        }

        if (labelEditing) {
            QGraphicsScene *scene = labelEditing->getScene();
            if (scene) {
                labelEditing->setSceneRect(scene->sceneRect());
                labelEditing->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
            }
        }
    }

private:
    QTableWidget *labelTable = nullptr;
    QHBoxLayout *mainLayout = nullptr;
    QSplitter *splitter = nullptr;
};
#endif // LABELEDITING_H
