#ifndef STYLEMANAGER_H
#define STYLEMANAGER_H

#include <QString>

class QApplication;

class StyleManager
{
public:
    enum class Theme {
        Dark,
        Light
    };

    static void applyDefault(QApplication &app);
    static void applyTheme(QApplication &app, Theme theme);

private:
    static QString loadResourceText(const QString &resourcePath);
    static void ensureFusionStyle(QApplication &app);
};

#endif // STYLEMANAGER_H
