//
// Created by Mac Liu on 2/7/22.
//

#include <time.h>
#include <stdio.h>
#include "logger.h"

static void console_writer  (const char *file, const int line,
                             const enum log_priority_t p,
                             const char *fmt, va_list args) {
    const char *priorityName = logger_get_priority_name (p);

    struct tm now;
    time_t currentTime;
    char buffer[8192];

    time (&currentTime);
    localtime_r (&currentTime, &now);

    vsnprintf (buffer, sizeof buffer - 1, fmt, args);
    buffer[sizeof buffer - 1] = '\0';

    if (p <= log_error) {
        fprintf (stderr, "%04d-%02d-%02d %02d:%02d:%02d [%s] %s [ at %s:%d ]\n",
                 now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                 now.tm_hour, now.tm_min, now.tm_sec,
                 priorityName, buffer,
                 file, line);
    } else {
        fprintf (stderr, "%04d-%02d-%02d %02d:%02d:%02d [%s] %s\n",
                 now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                 now.tm_hour, now.tm_min, now.tm_sec,
                 priorityName, buffer);
    }
}

struct log_appender_t	excalibur_console_appender = {
    .write = console_writer
};