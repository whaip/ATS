#include "deviceportmanagerwidget.h"
#include "ui_deviceportmanager.h"

deviceportmanager::deviceportmanager(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::deviceportmanager)
{
    ui->setupUi(this);
}

deviceportmanager::~deviceportmanager()
{
    delete ui;
}
