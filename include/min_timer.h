#pragma once

#include <sys/time.h>

struct minute_timer_t {
    int (*get_day_of_year) (void);
    int (*get_prev_day_of_year) (void);
    void (*terminate) (void);
    void (*start) (void (*func) (const struct timeval *, const struct tm *)) ;
};

struct minute_timer_t *new_minite_timer();
