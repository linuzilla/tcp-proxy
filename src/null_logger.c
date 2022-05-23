#include <stdio.h>
#include "logger.h"

static void null_log (const char *file, const int line,
                      const enum log_priority_t p, const char *fmt, ...) {
}

static void null_everything (const char *file, const int line, const char *fmt, ...) {
}

static enum log_priority_t null_getPriority (void) {
    return 0;
}

static enum log_priority_t null_setPriority (const enum log_priority_t p) {
    return 0;
}

static void null_clearAppender () {
}

static void null_addAppender (struct log_appender_t *appender) {
}

struct logger_t	excalibur_null_logger = {
    .log = null_log,
    .fatal = null_everything,
    .error = null_everything,
    .warning = null_everything,
    .notice = null_everything,
    .info = null_everything,
    .debug = null_everything,
    .trace = null_everything,
    .getPriority = null_getPriority,
    .setPriority = null_setPriority,
    .clearAppender = null_clearAppender,
    .addAppender = null_addAppender
};

