#include "logger.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>

namespace {

QMutex g_mutex;
QFile g_file;
QtMessageHandler g_prevHandler = nullptr;
bool g_initialized = false;
LogDispatcher *g_dispatcher = nullptr;

QString levelToString(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("CRITICAL");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

QString levelToString(Logger::Level level)
{
    switch (level) {
    case Logger::Level::Normal:
        return QStringLiteral("NORMAL");
    case Logger::Level::Debug:
        return QStringLiteral("DEBUG");
    case Logger::Level::Info:
        return QStringLiteral("INFO");
    case Logger::Level::Warn:
        return QStringLiteral("WARN");
    case Logger::Level::Error:
        return QStringLiteral("ERROR");
    case Logger::Level::Critical:
        return QStringLiteral("CRITICAL");
    }
    return QStringLiteral("UNKNOWN");
}

QString resolveLogDir()
{
    // Save logs next to the running executable: <exeDir>/logs
    QString base = QCoreApplication::applicationDirPath();
    if (base.isEmpty()) {
        base = QDir::currentPath();
    }

    QDir dir(base);
    if (!dir.mkpath(QStringLiteral("logs")) || !dir.cd(QStringLiteral("logs"))) {
        QDir fallback(QDir::currentPath());
        fallback.mkpath(QStringLiteral("logs"));
        fallback.cd(QStringLiteral("logs"));
        return fallback.absolutePath();
    }
    return dir.absolutePath();
}

QString resolveLogFilePath()
{
    const QString dir = resolveLogDir();
    const QString app = QCoreApplication::applicationName().isEmpty()
        ? QStringLiteral("ATS")
        : QCoreApplication::applicationName();

    const QString date = QDate::currentDate().toString(QStringLiteral("yyyyMMdd"));
    return QDir(dir).filePath(QStringLiteral("%1_%2.log").arg(app, date));
}

bool ensureLogFileOpenLocked()
{
    if (g_file.isOpen()) {
        return true;
    }

    // Ensure applicationName is set for log naming.
    if (QCoreApplication::applicationName().isEmpty()) {
        QCoreApplication::setApplicationName(QStringLiteral("ATS"));
    }

    const QString path = resolveLogFilePath();
    g_file.setFileName(path);
    return g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void writeLineLocked(const QString &line)
{
    if (g_file.isOpen()) {
        g_file.write(line.toUtf8());
        g_file.write("\r\n");
        g_file.flush();
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString level = levelToString(type);

    QString where;
    if (ctx.file && *ctx.file) {
        where = QStringLiteral("%1:%2").arg(QString::fromUtf8(ctx.file)).arg(ctx.line);
    }

    QString category;
    if (ctx.category && *ctx.category) {
        category = QString::fromUtf8(ctx.category);
    }

    QString func;
    if (ctx.function && *ctx.function) {
        func = QString::fromUtf8(ctx.function);
    }

    QString line = QStringLiteral("%1 [%2]").arg(ts, level);
    if (!category.isEmpty()) {
        line += QStringLiteral(" [%1]").arg(category);
    }
    if (!where.isEmpty()) {
        line += QStringLiteral(" [%1]").arg(where);
    }
    if (!func.isEmpty()) {
        line += QStringLiteral(" [%1]").arg(func);
    }
    line += QStringLiteral(" %1").arg(msg);

    LogDispatcher *dispatcher = nullptr;
    {
        QMutexLocker locker(&g_mutex);
        writeLineLocked(line);
        dispatcher = g_dispatcher;
    }

    if (dispatcher) {
        // Ensure UI updates happen on the dispatcher's thread.
        const QString tsCopy = ts;
        const QString levelCopy = level;
        const QString msgCopy = msg;
        QMetaObject::invokeMethod(
            dispatcher,
            [dispatcher, tsCopy, levelCopy, msgCopy]() {
                emit dispatcher->logAdded(tsCopy, levelCopy, msgCopy);
            },
            Qt::QueuedConnection);
    }

    if (g_prevHandler) {
        g_prevHandler(type, ctx, msg);
    }

    if (type == QtFatalMsg) {
        abort();
    }
}

} // namespace

LogDispatcher *Logger::dispatcher()
{
    QMutexLocker locker(&g_mutex);
    if (!g_dispatcher) {
        // Parent to the application object so it gets cleaned up automatically.
        QObject *app = QCoreApplication::instance();
        g_dispatcher = new LogDispatcher(app);
        if (app) {
            g_dispatcher->moveToThread(app->thread());
        }
    }
    return g_dispatcher;
}

void Logger::init()
{
    QMutexLocker locker(&g_mutex);
    if (g_initialized) {
        return;
    }

    // Ensure dispatcher exists for UI connections.
    if (!g_dispatcher) {
        locker.unlock();
        (void)Logger::dispatcher();
        locker.relock();
    }

    // Ensure applicationName is set for log naming.
    if (QCoreApplication::applicationName().isEmpty()) {
        QCoreApplication::setApplicationName(QStringLiteral("ATS"));
    }

    const QString path = resolveLogFilePath();
    qDebug() << "Log file path:" << path;

    g_file.setFileName(path);
    if (!g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        // Even if file can't be opened, still install handler to avoid missing logs.
        g_prevHandler = qInstallMessageHandler(messageHandler);
        g_initialized = true;
        return;
    }

    const QString header = QStringLiteral("----- %1 start (pid=%2) -----")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
        .arg(QCoreApplication::applicationPid());
    writeLineLocked(header);

    g_prevHandler = qInstallMessageHandler(messageHandler);
    g_initialized = true;
}

void Logger::log(const QString &message, Level level)
{
    if (!g_initialized) {
        // init() is internally locked and idempotent.
        Logger::init();
    }

    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString lvl = levelToString(level);
    const QString line = QStringLiteral("%1 [%2] %3").arg(ts, lvl, message);

    LogDispatcher *dispatcher = nullptr;

    QMutexLocker locker(&g_mutex);
    if (!ensureLogFileOpenLocked()) {
        return;
    }
    writeLineLocked(line);

    dispatcher = g_dispatcher;
    locker.unlock();

    if (dispatcher) {
        const QString tsCopy = ts;
        const QString lvlCopy = lvl;
        const QString msgCopy = message;
        QMetaObject::invokeMethod(
            dispatcher,
            [dispatcher, tsCopy, lvlCopy, msgCopy]() {
                emit dispatcher->logAdded(tsCopy, lvlCopy, msgCopy);
            },
            Qt::QueuedConnection);
    }
}

void Logger::flush()
{
    QMutexLocker locker(&g_mutex);
    if (g_file.isOpen()) {
        g_file.flush();
    }
}
