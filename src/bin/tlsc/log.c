#include "log.h"
#include "threadpool.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>

static LogWriter currentwriter = 0;
static void *writerdata;
static LogLevel maxlevel = L_INFO;
static int logsilent = 0;
static int logasync = 0;

static const char *levels[] =
{
    "[FATAL]",
    "[ERROR]",
    "[WARN ]",
    "[INFO ]",
    "[DEBUG]"
};

static int syslogLevels[] =
{
    LOG_CRIT,
    LOG_ERR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG
};

typedef struct LogJobArgs
{
    LogLevel level;
    LogWriter writer;
    void *writerdata;
    char message[];
} LogJobArgs;

static void logmsgJobProc(void *arg);
static void writeFile(LogLevel level, const char *message, void *data)
    ATTR_NONNULL((2));
static void writeSyslog(LogLevel level, const char *message, void *data)
    ATTR_NONNULL((2));

static void logmsgJobProc(void *arg)
{
    LogJobArgs *lja = arg;
    lja->writer(lja->level, lja->message, lja->writerdata);
    free(lja);
}

static void writeFile(LogLevel level, const char *message, void *data)
{
    FILE *target = data;
    fprintf(target, "%s  %s\n", levels[level], message);
    fflush(target);
}

static void writeSyslog(LogLevel level, const char *message, void *data)
{
    (void)data;
    syslog(syslogLevels[level], "%s", message);
}

SOEXPORT void Log_setFileLogger(FILE *file)
{
    currentwriter = writeFile;
    writerdata = file;
}

SOEXPORT void Log_setSyslogLogger(const char *ident,
	int facility, int withStderr)
{
    int logopts = LOG_PID;
    if (withStderr) logopts |= LOG_PERROR;
    openlog(ident, logopts, facility);
    currentwriter = writeSyslog;
    writerdata = 0;
}

SOEXPORT void Log_setCustomLogger(LogWriter writer, void *data)
{
    currentwriter = writer;
    writerdata = data;
}

SOEXPORT void Log_setMaxLogLevel(LogLevel level)
{
    maxlevel = level;
}

SOEXPORT void Log_setSilent(int silent)
{
    logsilent = silent;
}

SOEXPORT void Log_setAsync(int async)
{
    logasync = async;
}

SOEXPORT void Log_msg(LogLevel level, const char *message)
{
    if (!currentwriter) return;
    if (logsilent && level > L_ERROR) return;
    if (level > maxlevel) return;
    if (logasync && ThreadPool_active())
    {
	size_t msgsize = strlen(message)+1;
	LogJobArgs *lja = xmalloc(sizeof *lja + msgsize);
	lja->level = level;
	lja->writer = currentwriter;
	lja->writerdata = writerdata;
	strcpy(lja->message, message);
	ThreadJob *job = ThreadJob_create(logmsgJobProc, lja, 8);
	ThreadPool_enqueue(job);
    }
    else currentwriter(level, message, writerdata);
}

SOEXPORT void Log_fmt(LogLevel level, const char *format, ...)
{
    if (!currentwriter) return;
    if (logsilent && level > L_ERROR) return;
    if (level > maxlevel) return;
    char buf[MAXLOGLINE];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, MAXLOGLINE, format, ap);
    va_end(ap);
    Log_msg(level, buf);
}

