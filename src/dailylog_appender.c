#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include "logger.h"

static FILE *fp = NULL;
static const char *path = NULL;
static const char *prefix = NULL;
static const char *subfix = NULL;
static int yearday = -1;

static void dailylog_write (const char *file, const int line,
                            const enum log_priority_t p,
                            const char *fmt, va_list args) {
    struct tm tm;
    time_t now;

    time (&now);
    localtime_r (&now, &tm);

#define TEST_USING_MINUTE    0

#if TEST_USING_MINUTE
    if (tm.tm_min != yearday) {
#else
    if (tm.tm_yday != yearday) {
#endif
        int fnsize;
        char *fname;

#if TEST_USING_MINUTE
        yearday = tm.tm_min;
#else
        yearday = tm.tm_yday;
#endif

        if (fp != NULL) {
            fclose (fp);
            fp = NULL;
        }

        fnsize = strlen (path) + strlen (prefix) + strlen (subfix) + 40;
        fname = alloca (fnsize);
        snprintf (fname, fnsize - 1, "%s/%s-%04d%02d%02d-%02d%02d%02d.%s",
                  path, prefix, tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  subfix);
        fp = fopen (fname, "w");
    }

    if (fp != NULL) {
        vfprintf (fp, fmt, args);
        fprintf (fp, "\n\tat %s(%d)\n", file, line);
        fflush (fp);
    }
}

static struct log_appender_t appender = {
    .write = dailylog_write
};

struct log_appender_t *init_dailylog_appender (const char *filepath,
        const char *fileprefix, const char *filesubfix) {
    path = filepath;
    prefix = fileprefix;
    subfix = filesubfix;

    return &appender;
}
