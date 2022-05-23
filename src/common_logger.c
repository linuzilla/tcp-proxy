#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <strings.h>
#include "context.h"
#include "logger.h"

struct appender_list_t {
    struct log_appender_t *appender;
    struct appender_list_t *next;
};

struct log_level_t {
    char *name;
    enum log_priority_t priority;
};

static struct log_level_t logLevels[] = {
    {
        .priority = log_fatal,
        .name = "fatal"
    },
    {
        .priority = log_error,
        .name = "error"
    },
    {
        .priority = log_warning,
        .name = "warning"
    },
    {
        .priority = log_notice,
        .name = "notice"
    },
    {
        .priority = log_info,
        .name = "info"
    },
    {
        .priority = log_debug,
        .name = "debug"
    },
    {
        .priority = log_trace,
        .name = "trace"
    },
};

static struct appender_list_t *appender_list = NULL;
static enum log_priority_t priority = log_info;
static bool have_initialized = false;

static void con_dolog (const char *file, const int line,
                       const enum log_priority_t p, const char *fmt, va_list args) {
    if (p > priority) return;

    if (!have_initialized) {
        have_initialized = true;
        excalibur_common_logger.addAppender (&excalibur_stderr_appender);
    }

    struct appender_list_t *ptr = appender_list;

    while (ptr != NULL) {
        va_list dest;
        va_copy (dest, args);
        ptr->appender->write (file, line, p, fmt, dest);
        ptr = ptr->next;
    }
}

static void con_log (const char *file, const int line,
                     const enum log_priority_t p, const char *fmt, ...) {
    va_list args;
    va_start (args, fmt);
    con_dolog (file, line, p, fmt, args);
    va_end (args);
}

static void con_fatal (const char *file, const int line, const char *fmt, ...) {
    va_list args;
    va_start (args, fmt);
    con_dolog (file, line, log_fatal, fmt, args);
    va_end (args);
}

static void con_error (const char *file, const int line, const char *fmt, ...) {
    va_list args;
    va_start (args, fmt);
    con_dolog (file, line, log_error, fmt, args);
    va_end (args);
}

static void con_warning (const char *file, const int line, const char *fmt, ...) {
    va_list args;
    va_start (args, fmt);
    con_dolog (file, line, log_warning, fmt, args);
    va_end (args);
}

static void con_notice (const char *file, const int line, const char *fmt, ...) {
    va_list args;
    va_start (args, fmt);
    con_dolog (file, line, log_notice, fmt, args);
    va_end (args);
}

static void con_info (const char *file, const int line, const char *fmt, ...) {
    va_list args;
    va_start (args, fmt);
    con_dolog (file, line, log_info, fmt, args);
    va_end (args);
}

static void con_debug (const char *file, const int line, const char *fmt, ...) {
    va_list args;
    va_start (args, fmt);
    con_dolog (file, line, log_debug, fmt, args);
    va_end (args);
}

static void con_trace (const char *file, const int line, const char *fmt, ...) {
    va_list args;
    va_start (args, fmt);
    con_dolog (file, line, log_trace, fmt, args);
    va_end (args);
}

static enum log_priority_t con_getPriority (void) {
    return priority;
}

static enum log_priority_t con_setPriority (const enum log_priority_t p) {
    enum log_priority_t prev = priority;
    priority = p;
    return prev;
}

static void con_clearAppender() {
    struct appender_list_t *ptr;

    while (appender_list != NULL) {
        ptr = appender_list;
        appender_list = appender_list->next;
        free (ptr);
    }
}

static void con_addAppender (struct log_appender_t *appender) {
    struct appender_list_t *ptr;

    if ((ptr = malloc (sizeof * ptr)) != NULL) {
        ptr->next = appender_list;
        ptr->appender = appender;
        appender_list = ptr;
        have_initialized = true;
    }
}

static bool con_isEnable (const enum log_priority_t p) {
    return priority >= p;
}

struct logger_t excalibur_common_logger = {
    .log = con_log,
    .fatal = con_fatal,
    .error = con_error,
    .warning = con_warning,
    .notice = con_notice,
    .info = con_info,
    .debug = con_debug,
    .trace = con_trace,
    .getPriority = con_getPriority,
    .setPriority = con_setPriority,
    .clearAppender = con_clearAppender,
    .addAppender = con_addAppender,
    .isEnable = con_isEnable,
};


enum log_priority_t logger_get_priority_by_name (const char *name) {
    int i;

    for (i = 0; i < sizeof (logLevels) / sizeof (struct log_level_t); i++) {
        if (strcasecmp (name, logLevels[i].name) == 0) {
            return logLevels[i].priority;
        }
    }
    return log_notice;
}

const char* logger_get_priority_name (enum log_priority_t p) {
    int i;

    for (i = 0; i < sizeof (logLevels) / sizeof (struct log_level_t); i++) {
        if (logLevels[i].priority == p) {
            return logLevels[i].name;
        }
    }
    return "";
}
