#include "configurationwindow.h"
#include "ui_configurationwindow.h"

ConfigurationWindow::ConfigurationWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ConfigurationWindow)
{
    ui->setupUi(this);
}

ConfigurationWindow::~ConfigurationWindow()
{
    delete ui;
}
