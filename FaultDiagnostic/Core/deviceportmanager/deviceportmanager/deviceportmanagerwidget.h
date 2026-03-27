#ifndef DEVICEPORTMANAGER_WIDGET_H
#define DEVICEPORTMANAGER_WIDGET_H

#include <QWidget>

namespace Ui {
class deviceportmanager;
}

class deviceportmanager : public QWidget
{
    Q_OBJECT

public:
    explicit deviceportmanager(QWidget *parent = nullptr);
    ~deviceportmanager();

private:
    Ui::deviceportmanager *ui;
};

#endif // DEVICEPORTMANAGER_WIDGET_H
