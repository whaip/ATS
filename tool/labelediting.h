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
#include <QOpenGLWidget>
#include <QTableWidget>
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
        return -1;  // 返回-1表示没有选中的项
    }

    QGraphicsScene* getScene() const { return scene; }

    // 添加获取所有被选中标签信息的方法
    std::vector<Label> getSelectedLabelItemInfos() const;

    LabelRectItem* getRectItemById(int id) const;    // 外部调用以刷新表格内容（内部调用 updateLabelTable）
    Q_INVOKABLE void refreshTable();
    
    // 完全清理和重置组件状态（用于板卡切换）
    void fullReset();

public slots:
    void on_createRectButton_clicked();
    void on_editButton_clicked();
    void on_finishButton_clicked();
    void on_deleteButton_clicked();

signals:
    void window_close();
    void closeRequested();  // 添加新的信号
    
    // 标签操作同步信号
    void labelAdded(const Label& label);      // 标签添加信号
    void labelUpdated(const Label& label);    // 标签更新信号  
    void labelDeleted(int labelId);           // 标签删除信号
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
};

class LabelEditingWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LabelEditingWindow(QWidget *parent, const QImage &image, std::vector<Label> label_info, std::vector<Label> label_info_add, std::vector<int>& delete_id)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        mainLayout = new QHBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);        labelTable = new QTableWidget(this);
        labelEditing = std::make_shared<LabelEditing>(this, image, label_info, label_info_add, delete_id, labelTable);
        connect(labelEditing.get(), &LabelEditing::closeRequested, this, &QWidget::close);

        // 转发标签操作信号
        connect(labelEditing.get(), &LabelEditing::labelAdded, this, &LabelEditingWindow::labelAdded);
        connect(labelEditing.get(), &LabelEditing::labelUpdated, this, &LabelEditingWindow::labelUpdated);
        connect(labelEditing.get(), &LabelEditing::labelDeleted, this, &LabelEditingWindow::labelDeleted);

        labelTable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        labelEditing->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

        mainLayout->addWidget(labelEditing.get(), 3);  // 分配更多空间给编辑区
        mainLayout->addWidget(labelTable, 1);  // 分配较少空间给标签表
        setLayout(mainLayout);
    }
    ~LabelEditingWindow() override
    {
        delete labelTable;
        delete mainLayout;
    }

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

    int getSelectedLabelId() const {
        return labelEditing->getSelectedLabelId();
    }

    std::shared_ptr<LabelEditing> labelEditing;
signals:
    void window_close();
    
    // 转发标签操作信号
    void labelAdded(const Label& label);
    void labelUpdated(const Label& label);  
    void labelDeleted(int labelId);
protected:
    void closeEvent(QCloseEvent *event) override
    {
        emit window_close();
        event->accept();
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        
        // 确保布局正确更新
        if (mainLayout) {
            mainLayout->update();
        }
        
        // 确保编辑区域正确调整大小
        if (labelEditing) {
            QGraphicsScene* scene = labelEditing->getScene();
            if (scene) {
                labelEditing->setSceneRect(scene->sceneRect());
                labelEditing->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
            }
        }
    }
private:
    QTableWidget* labelTable;
    QHBoxLayout* mainLayout;
};

#endif // LABELEDITING_H
