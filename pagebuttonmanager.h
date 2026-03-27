#ifndef PAGEBUTTONMANAGER_H
#define PAGEBUTTONMANAGER_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class QButtonGroup;
class QHBoxLayout;
class QScrollArea;
class QStackedWidget;

class PageButtonManager : public QObject
{
    Q_OBJECT

public:
    explicit PageButtonManager(QStackedWidget *stack, QHBoxLayout *buttonsLayout, QObject *parent = nullptr);

    void rebuild();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void clearButtons();
    void updateChecked(int index);
    QString pageDisplayName(int index) const;
    QScrollArea *findScrollArea() const;
    bool handleWheelScroll(QEvent *event);
    void ensureCurrentButtonVisible(int index);

    QPointer<QStackedWidget> m_stack;
    QPointer<QHBoxLayout> m_layout;
    QButtonGroup *m_group = nullptr;
    QVector<QObject *> m_buttons;
};

#endif // PAGEBUTTONMANAGER_H
