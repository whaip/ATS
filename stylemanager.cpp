#include "stylemanager.h"

#include <QApplication>
#include <QFile>
#include <QStyleFactory>
#include <QString>

QString StyleManager::loadResourceText(const QString &resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

void StyleManager::ensureFusionStyle(QApplication &app)
{
    // Make widget metrics consistent across platforms.
    if (QStyle *fusion = QStyleFactory::create("Fusion")) {
        app.setStyle(fusion);
    }
}

void StyleManager::applyDefault(QApplication &app)
{
    applyTheme(app, Theme::Dark);
}

void StyleManager::applyTheme(QApplication &app, Theme theme)
{
    ensureFusionStyle(app);

    // Expose current theme for per-page styling.
    app.setProperty("atsTheme", (theme == Theme::Light) ? QStringLiteral("light") : QStringLiteral("dark"));

    const QString resourcePath = (theme == Theme::Light)
        ? QStringLiteral(":/styles/light.qss")
        : QStringLiteral(":/styles/default.qss");

    const QString qss = loadResourceText(resourcePath);
    if (!qss.isEmpty()) {
        app.setStyleSheet(qss);
    }
}
