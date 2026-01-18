#ifndef LOGGER_H
#define LOGGER_H

#include <QtGlobal>
#include <QString>

#include "logdispatcher.h"

class Logger
{
public:
    enum class Level {
        Normal,
        Debug,
        Info,
        Warn,
        Error,
        Critical
    };

    // Installs a global Qt message handler and starts writing logs to file.
    // Safe to call multiple times.
    static void init();

    // Signal source for UI. You can connect to LogDispatcher::logAdded.
    static LogDispatcher *dispatcher();

    // Writes one log line to file.
    // - time: auto-generated
    // - level: default Normal
    // - message: provided by caller
    static void log(const QString &message, Level level = Level::Normal);

    // Flushes buffered logs to disk.
    static void flush();

private:
    Logger() = delete;
};

#endif // LOGGER_H
