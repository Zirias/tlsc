#ifndef TLSC_LOG_H
#define TLSC_LOG_H

#include "decl.h"

#include <stdio.h>

#define MAXLOGLINE 16384

typedef enum LogLevel
{
    L_FATAL,
    L_ERROR,
    L_WARNING,
    L_INFO,
    L_DEBUG
} LogLevel;

typedef void (*LogWriter)(LogLevel level, const char *message, void *data)
    ATTR_NONNULL((2));

DECLEXPORT void Log_setFileLogger(FILE *file) ATTR_NONNULL((1));
DECLEXPORT void Log_setSyslogLogger(const char *ident, int facility,
	int withStderr) ATTR_NONNULL((1));
DECLEXPORT void Log_setCustomLogger(LogWriter writer, void *data)
    ATTR_NONNULL((1));
DECLEXPORT void Log_setMaxLogLevel(LogLevel level);
DECLEXPORT void Log_setSilent(int silent);
DECLEXPORT void Log_setAsync(int async);
DECLEXPORT void Log_msg(LogLevel level, const char *message)
    ATTR_NONNULL((2));
DECLEXPORT void Log_fmt(LogLevel level, const char *format, ...)
    ATTR_NONNULL((2)) ATTR_FORMAT((printf, 2, 3));

#endif

