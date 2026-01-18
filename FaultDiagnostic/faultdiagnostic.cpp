#include "faultdiagnostic.h"
#include "ui_faultdiagnostic.h"

#include "../IRCamera/ircamera.h"
#include "../IODevices/uestcqcustomplot.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QListWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QSplitter>

FaultDiagnostic::FaultDiagnostic(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FaultDiagnostic)
{
    ui->setupUi(this);
    buildWidgets();
    applyThemeQss();

    if (m_list) {
        connect(m_list, &QListWidget::currentRowChanged, this, &FaultDiagnostic::onComponentSelectionChanged);
    }
}

FaultDiagnostic::~FaultDiagnostic()
{
    delete ui;
}

void FaultDiagnostic::setComponents(const QVector<ComponentViewData> &items)
{
    m_components = items;
    m_indexById.clear();

    if (!m_list) {
        return;
    }

    m_list->clear();
    for (int i = 0; i < m_components.size(); ++i) {
        const auto &item = m_components[i];
        const QString label = item.name.isEmpty() ? item.id : item.name;
        m_list->addItem(label.isEmpty() ? QStringLiteral("未命名器件") : label);
        if (!item.id.isEmpty()) {
            m_indexById.insert(item.id, i);
        }
    }

    if (!m_components.isEmpty()) {
        m_list->setCurrentRow(0);
        setCurrentIndex(0);
    } else {
        refreshThermal(ComponentViewData{});
        refreshPlot(ComponentViewData{});
        refreshReport(ComponentViewData{});
    }
}

void FaultDiagnostic::updateComponent(const ComponentViewData &item)
{
    if (!m_list) {
        return;
    }

    const int existing = item.id.isEmpty() ? -1 : m_indexById.value(item.id, -1);
    if (existing >= 0 && existing < m_components.size()) {
        m_components[existing] = item;
        const QString label = item.name.isEmpty() ? item.id : item.name;
        if (auto *w = m_list->item(existing)) {
            w->setText(label.isEmpty() ? QStringLiteral("未命名器件") : label);
        }
        if (m_list->currentRow() == existing) {
            setCurrentIndex(existing);
        }
        return;
    }

    m_components.push_back(item);
    const int index = m_components.size() - 1;
    const QString label = item.name.isEmpty() ? item.id : item.name;
    m_list->addItem(label.isEmpty() ? QStringLiteral("未命名器件") : label);
    if (!item.id.isEmpty()) {
        m_indexById.insert(item.id, index);
    }
    if (m_list->currentRow() < 0) {
        m_list->setCurrentRow(index);
        setCurrentIndex(index);
    }
}

void FaultDiagnostic::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event) {
        return;
    }
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        applyThemeQss();
    }
}

void FaultDiagnostic::onComponentSelectionChanged()
{
    if (!m_list) {
        return;
    }
    setCurrentIndex(m_list->currentRow());
}

void FaultDiagnostic::buildWidgets()
{
    if (!ui) {
        return;
    }

    m_list = ui->listComponents;
    if (m_list) {
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    }

    if (ui->thermalContainer) {
        if (!ui->thermalContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->thermalContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_tempView = new IRCamera(ui->thermalContainer);
        m_tempView->setStationEnabled(false);

        if (auto *splitter = m_tempView->findChild<QSplitter *>(QStringLiteral("splitterMain"))) {
            splitter->setSizes({0, 1});
        }
        if (auto *leftPanel = m_tempView->findChild<QWidget *>(QStringLiteral("leftPanel"))) {
            leftPanel->setVisible(false);
        }

        ui->thermalContainer->layout()->addWidget(m_tempView);
    }

    if (ui->plotContainer) {
        if (!ui->plotContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->plotContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_plot = new UESTCQCustomPlot(ui->plotContainer);
        m_plot->setTimeAxisEnabled(false);
        m_plot->setAutoRangeEnabled(true);
        ui->plotContainer->layout()->addWidget(m_plot);
    }

    if (ui->reportContainer) {
        if (!ui->reportContainer->layout()) {
            auto *layout = new QVBoxLayout(ui->reportContainer);
            layout->setContentsMargins(0, 0, 0, 0);
        }
        m_report = new QTextBrowser(ui->reportContainer);
        m_report->setOpenExternalLinks(false);
        ui->reportContainer->layout()->addWidget(m_report);
    }
}

void FaultDiagnostic::applyThemeQss()
{
    const QString theme = qApp ? qApp->property("atsTheme").toString().toLower() : QString();
    if (m_applyingQss) {
        return;
    }
    if (!theme.isEmpty() && theme == m_loadedTheme) {
        return;
    }
    const QString qssPath = (theme == QStringLiteral("light"))
        ? QStringLiteral(":/styles/faultdiagnostic_light.qss")
        : QStringLiteral(":/styles/faultdiagnostic_dark.qss");
    QFile file(qssPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_applyingQss = true;
        setStyleSheet(QString::fromUtf8(file.readAll()));
        m_applyingQss = false;
        if (!theme.isEmpty()) {
            m_loadedTheme = theme;
        }
    }
}

void FaultDiagnostic::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_components.size()) {
        return;
    }
    const auto &item = m_components[index];
    refreshThermal(item);
    refreshPlot(item);
    refreshReport(item);
}

void FaultDiagnostic::refreshThermal(const ComponentViewData &item)
{
    if (!m_tempView) {
        return;
    }
    if (!item.thermalImage.isNull() && !item.thermalMatrix.isEmpty() && item.thermalMatrixSize.isValid()) {
        m_tempView->setInputData(item.thermalImage, item.thermalMatrix, item.thermalMatrixSize);
    } else {
        m_tempView->setInputData(QImage(), {}, QSize());
    }
}

void FaultDiagnostic::refreshPlot(const ComponentViewData &item)
{
    if (!m_plot) {
        return;
    }
    if (!m_staticGraph) {
        m_staticGraph = m_plot->addStaticLine(QStringLiteral("诊断数据"), item.x, item.y, QColor(52, 152, 219));
        return;
    }
    m_plot->updateStaticLine(m_staticGraph, item.x, item.y);
}

void FaultDiagnostic::refreshReport(const ComponentViewData &item)
{
    if (!m_report) {
        return;
    }
    if (item.reportHtml.isEmpty()) {
        m_report->clear();
        return;
    }
    m_report->setHtml(item.reportHtml);
}
