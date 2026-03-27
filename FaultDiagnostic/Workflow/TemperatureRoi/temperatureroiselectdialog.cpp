#include "temperatureroiselectdialog.h"

#include "../../../IRCamera/ircamera.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

TemperatureRoiSelectDialog::TemperatureRoiSelectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("测温区域框选"));
    resize(980, 760);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("请点击“框选”按钮后在图像上拖拽一个测温框。"), this));

    m_camera = new IRCamera(this);
    m_camera->setStationEnabled(true);
    layout->addWidget(m_camera, 1);

    if (auto *addBox = m_camera->findChild<QPushButton *>(QStringLiteral("btnAddBox"))) {
        addBox->setChecked(true);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &TemperatureRoiSelectDialog::onAcceptClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &TemperatureRoiSelectDialog::reject);
    layout->addWidget(buttons);
}

QRectF TemperatureRoiSelectDialog::selectedRoi() const
{
    return m_selectedRoi;
}

QVector<QRectF> TemperatureRoiSelectDialog::selectedRois() const
{
    return m_selectedRois;
}

bool TemperatureRoiSelectDialog::hasSelectedRoi() const
{
    return m_hasSelectedRoi;
}

void TemperatureRoiSelectDialog::onAcceptClicked()
{
    if (!m_camera) {
        reject();
        return;
    }

    const QVector<QRectF> rois = m_camera->boxRects();
    if (rois.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("测温区域"), QStringLiteral("请先框选至少一个测温区域。"));
        return;
    }

    m_selectedRois = rois;
    m_selectedRoi = rois.first();
    m_hasSelectedRoi = true;
    accept();
}
