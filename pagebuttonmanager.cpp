#include "pagebuttonmanager.h"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTimer>
#include <QWheelEvent>
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

    if ((event->type() == QEvent::Wheel) && handleWheelScroll(event)) {
        return true;
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

    if (!display.trimmed().isEmpty()) {
        return display.trimmed();
    }

    return QStringLiteral("页面%1").arg(index + 1);
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

    int totalButtonsWidth = 0;
    int maxButtonHeight = 30;
    for (int i = 0; i < count; ++i) {
        const QString displayName = pageDisplayName(i);
        auto *btn = new QPushButton(displayName);
        btn->setObjectName(QStringLiteral("pageNavButton"));
        btn->setCheckable(true);
        btn->setAutoDefault(false);
        btn->setDefault(false);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        btn->setMinimumHeight(30);
        btn->setMaximumHeight(30);

        const int textWidth = btn->fontMetrics().horizontalAdvance(displayName);
        const int buttonWidth = qBound(56, textWidth + 24, 220);
        btn->setFixedWidth(buttonWidth);
        totalButtonsWidth += buttonWidth;
        maxButtonHeight = qMax(maxButtonHeight, btn->height());

        const QString fullTitle = QStringLiteral("%1. %2").arg(i + 1).arg(displayName);
        btn->setToolTip(fullTitle);
        btn->installEventFilter(this);

        m_layout->addWidget(btn);
        m_group->addButton(btn, i);
        m_buttons.push_back(btn);
    }

    if (auto *container = m_layout->parentWidget()) {
        const QMargins margins = m_layout->contentsMargins();
        const int spacingWidth = (count > 1) ? (m_layout->spacing() * (count - 1)) : 0;
        const int width = qMax(1, margins.left() + totalButtonsWidth + spacingWidth + margins.right());
        const int height = qMax(36, margins.top() + maxButtonHeight + margins.bottom());
        container->setMinimumWidth(width);
        container->setMaximumWidth(width);
        container->setMinimumHeight(height);
        container->setMaximumHeight(height);
        container->resize(width, height);
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

    ensureCurrentButtonVisible(index);
    QTimer::singleShot(0, this, [this, index]() {
        ensureCurrentButtonVisible(index);
    });
}

QScrollArea *PageButtonManager::findScrollArea() const
{
    QWidget *widget = m_layout ? m_layout->parentWidget() : nullptr;
    while (widget) {
        if (auto *scrollArea = qobject_cast<QScrollArea *>(widget)) {
            return scrollArea;
        }
        widget = widget->parentWidget();
    }
    return nullptr;
}

bool PageButtonManager::handleWheelScroll(QEvent *event)
{
    auto *wheelEvent = dynamic_cast<QWheelEvent *>(event);
    if (!wheelEvent) {
        return false;
    }

    QScrollArea *scrollArea = findScrollArea();
    if (!scrollArea) {
        return false;
    }

    QScrollBar *bar = scrollArea->horizontalScrollBar();
    if (!bar || bar->maximum() <= 0) {
        return false;
    }

    int delta = wheelEvent->angleDelta().y();
    if (delta == 0) {
        delta = wheelEvent->angleDelta().x();
    }
    if (delta == 0) {
        delta = wheelEvent->pixelDelta().y();
    }
    if (delta == 0) {
        delta = wheelEvent->pixelDelta().x();
    }
    if (delta == 0) {
        return false;
    }

    const int step = qBound(16, qAbs(delta) / 2, 120);
    const int next = bar->value() + (delta > 0 ? -step : step);
    bar->setValue(next);
    return true;
}

void PageButtonManager::ensureCurrentButtonVisible(int index)
{
    if (!m_group) {
        return;
    }

    auto *button = m_group->button(index);
    auto *widget = qobject_cast<QWidget *>(button);
    if (!widget) {
        return;
    }

    QScrollArea *scrollArea = findScrollArea();
    if (!scrollArea || !scrollArea->viewport()) {
        return;
    }

    QScrollBar *bar = scrollArea->horizontalScrollBar();
    if (!bar) {
        return;
    }

    const int viewportWidth = scrollArea->viewport()->width();
    if (viewportWidth <= 0) {
        return;
    }

    const int left = widget->x();
    const int right = left + widget->width();
    const int viewLeft = bar->value();
    const int viewRight = viewLeft + viewportWidth;
    const int padding = 12;

    if (left < viewLeft + padding) {
        bar->setValue(left - padding);
    } else if (right > viewRight - padding) {
        bar->setValue(right - viewportWidth + padding);
    }
}
