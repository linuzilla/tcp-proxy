#pragma once

#include <stdarg.h>
#include <stdbool.h>

enum log_priority_t {
    log_fatal = 1,
    log_error = 2,
    log_warning = 3,
    log_notice = 4,
    log_info = 5,
    log_debug = 6,
    log_trace = 7
};

struct log_appender_t {
    void (*write) (const char *file, const int line,
                   const enum log_priority_t p,
                   const char *fmt, va_list args);
};

struct logger_t {
    void (*log) (const char *file, const int line, const enum log_priority_t p, const char *fmt, ...);

    void (*fatal) (const char *file, const int line, const char *fmt, ...);

    void (*error) (const char *file, const int line, const char *fmt, ...);

    void (*warning) (const char *file, const int line, const char *fmt, ...);

    void (*notice) (const char *file, const int line, const char *fmt, ...);

    void (*info) (const char *file, const int line, const char *fmt, ...);

    void (*debug) (const char *file, const int line, const char *fmt, ...);

    void (*trace) (const char *file, const int line, const char *fmt, ...);

    enum log_priority_t (*getPriority) (void);

    enum log_priority_t (*setPriority) (const enum log_priority_t p);

    void (*clearAppender) (void);

    void (*addAppender) (struct log_appender_t *appender);

    bool (*isEnable) (const enum log_priority_t priority);
};


extern struct logger_t excalibur_common_logger;
extern struct logger_t excalibur_null_logger;
extern struct log_appender_t excalibur_stderr_appender;
extern struct log_appender_t excalibur_console_appender;

extern enum log_priority_t logger_get_priority_by_name (const char *name);
extern const char* logger_get_priority_name (enum log_priority_t priority);

extern struct log_appender_t *init_dailylog_appender (
    const char *filepath,
    const char *fileprefix, const char *filesubfix);
