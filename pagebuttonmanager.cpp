#include "pagebuttonmanager.h"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>

PageButtonManager::PageButtonManager(QStackedWidget *stack, QHBoxLayout *buttonsLayout, QObject *parent)
    : QObject(parent)
    , m_stack(stack)
    , m_layout(buttonsLayout)
{
    if (m_stack) {
        m_stack->installEventFilter(this);
        connect(m_stack, &QStackedWidget::currentChanged, this, [this](int index) {
            updateChecked(index);
        });
#if (QT_VERSION >= QT_VERSION_CHECK(5, 4, 0))
        connect(m_stack, &QStackedWidget::widgetRemoved, this, [this](int) {
            rebuild();
        });
#endif
    }

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    connect(m_group, &QButtonGroup::idClicked, this, [this](int id) {
        if (m_stack && id >= 0 && id < m_stack->count()) {
            m_stack->setCurrentIndex(id);
        }
    });

    rebuild();
}

bool PageButtonManager::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_stack) {
        switch (event->type()) {
        case QEvent::ChildAdded:
        case QEvent::ChildRemoved:
        case QEvent::LayoutRequest:
            rebuild();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

void PageButtonManager::clearButtons()
{
    for (QObject *obj : m_buttons) {
        if (auto *w = qobject_cast<QWidget *>(obj)) {
            m_group->removeButton(qobject_cast<QAbstractButton *>(w));
            w->deleteLater();
        }
    }
    m_buttons.clear();

    if (!m_layout) {
        return;
    }

    while (QLayoutItem *item = m_layout->takeAt(0)) {
        // Widgets are deleted via deleteLater above.
        delete item;
    }
}

QString PageButtonManager::pageDisplayName(int index) const
{
    if (!m_stack || index < 0 || index >= m_stack->count()) {
        return {};
    }

    QWidget *page = m_stack->widget(index);
    if (!page) {
        return {};
    }

    const QString title = page->windowTitle();
    const QString name = page->objectName();
    const QString display = !title.isEmpty() ? title : name;

    return QStringLiteral("%1 %2").arg(index + 1).arg(display);
}

void PageButtonManager::rebuild()
{
    if (!m_stack || !m_layout) {
        return;
    }

    clearButtons();

    const int count = m_stack->count();
    if (auto *container = m_layout->parentWidget()) {
        container->setVisible(count > 0);
    }

    for (int i = 0; i < count; ++i) {
        auto *btn = new QPushButton(pageDisplayName(i));
        btn->setCheckable(true);
        btn->setAutoDefault(false);
        btn->setDefault(false);
        m_layout->addWidget(btn);
        m_group->addButton(btn, i);
        m_buttons.push_back(btn);
    }

    updateChecked(m_stack->currentIndex());
}

void PageButtonManager::updateChecked(int index)
{
    if (!m_stack) {
        return;
    }

    if (index < 0 || index >= m_stack->count()) {
        return;
    }

    if (auto *btn = m_group->button(index)) {
        btn->setChecked(true);
    }
}
