#include <stdio.h>
#include "logger.h"

static void stderr_write  (const char *file, const int line,
                           const enum log_priority_t p,
                           const char *fmt, va_list args) {
    const char *priorityName = logger_get_priority_name (p);
    vfprintf (stderr, fmt, args);
    fprintf (stderr, "\n\tat %s (%d) [%s]\n", file, line, priorityName);
}

struct log_appender_t	excalibur_stderr_appender = {
    .write = stderr_write
};
