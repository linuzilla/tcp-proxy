//
// Created by Mac Liu on 12/6/21.
//

#include <stdio.h>
#include <syslog.h>
#include "logger.h"
#include "syslog_appender.h"

static char *ident = "tcp-proxy";
static int facility = LOG_LOCAL6;

static void syslog_write (const char *file, const int line,
                          const enum log_priority_t p,
                          const char *fmt, va_list args) {

    const char *priorityName = logger_get_priority_name (p);

    char buffer[8192];

    vsnprintf (buffer, sizeof buffer - 1, fmt, args);
    buffer[sizeof buffer - 1] = '\0';

    openlog (ident, 0, facility);
    if (p <= log_error) {
        syslog (LOG_NOTICE, "[%s] %s [ at %s:%d ]", priorityName, buffer, file, line);
    } else {
        syslog (LOG_NOTICE, "[%s] %s", priorityName, buffer);
    }
    closelog();
}

struct log_appender_t excalibur_syslog_appender = {
    .write = syslog_write
};
