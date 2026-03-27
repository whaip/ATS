#include "batchparamreviewdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString taskDisplayText(const BatchParamReviewItem &item, int index)
{
    const QString ref = item.componentRef.trimmed().isEmpty()
        ? QStringLiteral("item_%1").arg(index + 1)
        : item.componentRef.trimmed();
    return QStringLiteral("%1 (%2)").arg(ref, item.pluginId);
}

void configureSpinRange(QSpinBox *editor, const TPSParamDefinition &definition)
{
    const int minValue = definition.minValue.isValid() ? definition.minValue.toInt() : -1000000000;
    const int maxValue = definition.maxValue.isValid() ? definition.maxValue.toInt() : 1000000000;
    const int stepValue = definition.stepValue.isValid() ? qMax(1, definition.stepValue.toInt()) : 1;
    editor->setMinimum(minValue);
    editor->setMaximum(maxValue);
    editor->setSingleStep(stepValue);
}

void configureDoubleRange(QDoubleSpinBox *editor, const TPSParamDefinition &definition)
{
    const double minValue = definition.minValue.isValid() ? definition.minValue.toDouble() : -1e12;
    const double maxValue = definition.maxValue.isValid() ? definition.maxValue.toDouble() : 1e12;
    const double stepValue = definition.stepValue.isValid() ? qMax(0.000001, definition.stepValue.toDouble()) : 0.1;
    editor->setDecimals(6);
    editor->setMinimum(minValue);
    editor->setMaximum(maxValue);
    editor->setSingleStep(stepValue);
}
}

BatchParamReviewDialog::BatchParamReviewDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("参数审核与修改"));
    resize(980, 640);

    auto *mainLayout = new QVBoxLayout(this);
    auto *hint = new QLabel(QStringLiteral("请逐个审核元件参数，确认后进入端口分配与后续流程。"), this);
    mainLayout->addWidget(hint);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_taskList = new QListWidget(splitter);
    m_taskList->setMinimumWidth(260);

    m_paramTable = new QTableWidget(splitter);
    m_paramTable->setColumnCount(3);
    m_paramTable->setHorizontalHeaderLabels({QStringLiteral("参数键"), QStringLiteral("参数名"), QStringLiteral("值")});
    m_paramTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_paramTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_paramTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_paramTable->verticalHeader()->setVisible(false);
    m_paramTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_paramTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &BatchParamReviewDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &BatchParamReviewDialog::reject);
    mainLayout->addWidget(buttons);

    connect(m_taskList, &QListWidget::currentRowChanged, this, &BatchParamReviewDialog::onCurrentTaskChanged);
}

void BatchParamReviewDialog::setItems(const QVector<BatchParamReviewItem> &items)
{
    m_items = items;
    m_taskList->clear();

    for (int i = 0; i < m_items.size(); ++i) {
        m_taskList->addItem(taskDisplayText(m_items[i], i));
    }

    if (!m_items.isEmpty()) {
        m_taskList->setCurrentRow(0);
    } else {
        m_paramTable->setRowCount(0);
        m_currentTaskIndex = -1;
    }
}

QVector<BatchParamReviewItem> BatchParamReviewDialog::reviewedItems() const
{
    return m_items;
}

void BatchParamReviewDialog::onCurrentTaskChanged(int row)
{
    saveCurrentTaskValues();
    loadTaskToTable(row);
}

void BatchParamReviewDialog::accept()
{
    saveCurrentTaskValues();
    QDialog::accept();
}

QWidget *BatchParamReviewDialog::createEditor(const TPSParamDefinition &definition,
                                              const QVariant &value,
                                              QWidget *parent) const
{
    switch (definition.type) {
    case TPSParamType::Boolean: {
        auto *editor = new QCheckBox(parent);
        editor->setChecked(value.toBool());
        return editor;
    }
    case TPSParamType::Integer: {
        auto *editor = new QSpinBox(parent);
        configureSpinRange(editor, definition);
        editor->setValue(value.toInt());
        return editor;
    }
    case TPSParamType::Double: {
        auto *editor = new QDoubleSpinBox(parent);
        configureDoubleRange(editor, definition);
        editor->setValue(value.toDouble());
        return editor;
    }
    case TPSParamType::Enum: {
        auto *editor = new QComboBox(parent);
        editor->addItems(definition.enumOptions);
        const int idx = editor->findText(value.toString());
        editor->setCurrentIndex(idx >= 0 ? idx : 0);
        return editor;
    }
    case TPSParamType::String:
    default: {
        auto *editor = new QLineEdit(parent);
        editor->setText(value.toString());
        return editor;
    }
    }
}

QVariant BatchParamReviewDialog::readEditorValue(const TPSParamDefinition &definition, QWidget *editor) const
{
    if (!editor) {
        return {};
    }

    switch (definition.type) {
    case TPSParamType::Boolean: {
        auto *check = qobject_cast<QCheckBox *>(editor);
        return check ? QVariant(check->isChecked()) : QVariant(false);
    }
    case TPSParamType::Integer: {
        auto *spin = qobject_cast<QSpinBox *>(editor);
        return spin ? QVariant(spin->value()) : QVariant(0);
    }
    case TPSParamType::Double: {
        auto *doubleSpin = qobject_cast<QDoubleSpinBox *>(editor);
        return doubleSpin ? QVariant(doubleSpin->value()) : QVariant(0.0);
    }
    case TPSParamType::Enum: {
        auto *combo = qobject_cast<QComboBox *>(editor);
        return combo ? QVariant(combo->currentText()) : QVariant(QString());
    }
    case TPSParamType::String:
    default: {
        auto *line = qobject_cast<QLineEdit *>(editor);
        return line ? QVariant(line->text()) : QVariant(QString());
    }
    }
}

void BatchParamReviewDialog::saveCurrentTaskValues()
{
    if (m_currentTaskIndex < 0 || m_currentTaskIndex >= m_items.size()) {
        return;
    }

    auto &item = m_items[m_currentTaskIndex];
    for (int row = 0; row < item.definitions.size() && row < m_currentEditors.size(); ++row) {
        const auto &definition = item.definitions[row];
        QWidget *editor = m_currentEditors[row];
        item.values.insert(definition.key, readEditorValue(definition, editor));
    }
}

void BatchParamReviewDialog::loadTaskToTable(int index)
{
    m_paramTable->setRowCount(0);
    m_currentEditors.clear();
    m_currentTaskIndex = index;

    if (index < 0 || index >= m_items.size()) {
        return;
    }

    const auto &item = m_items[index];
    m_paramTable->setRowCount(item.definitions.size());
    m_currentEditors.resize(item.definitions.size());

    for (int row = 0; row < item.definitions.size(); ++row) {
        const auto &definition = item.definitions[row];
        const QVariant value = item.values.value(definition.key, definition.defaultValue);

        auto *keyItem = new QTableWidgetItem(definition.key);
        auto *labelItem = new QTableWidgetItem(definition.label);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        labelItem->setFlags(labelItem->flags() & ~Qt::ItemIsEditable);
        m_paramTable->setItem(row, 0, keyItem);
        m_paramTable->setItem(row, 1, labelItem);

        QWidget *editor = createEditor(definition, value, m_paramTable);
        m_paramTable->setCellWidget(row, 2, editor);
        m_currentEditors[row] = editor;
    }
}
