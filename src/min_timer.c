#include <sys/time.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include "global_vars.h"
#include "min_timer.h"
#include "context.h"

static struct logger_t *logger = &excalibur_common_logger;
static int day_of_year = 0;
static int prev_day_of_year = 0;
static volatile bool terminate = false;
static pthread_t timer_thread;

static void init_minute_timer (void) {
    struct tm *tm;
    struct timeval tv;

    gettimeofday (&tv, NULL);
    tm = localtime (&tv.tv_sec);
    prev_day_of_year = day_of_year = tm->tm_yday;
}

static int get_day_of_year (void) {
    return day_of_year;
}

static int get_prev_day_of_year (void) {
    return prev_day_of_year;
}

static void terminate_minute_timer (void) {
    terminate = true;
    pthread_kill (timer_thread, SIGINT);
}

static void start_minute_timer (void (*func) (const struct timeval *, const struct tm *)) {
    const int time_unit = 5;
    struct timeval tv;
    long last_unit;
    long current_unit;
    struct tm *tm, tmvalue;

    timer_thread = pthread_self();

    tzset();
    init_minute_timer();

    gettimeofday (&tv, NULL);
    last_unit = tv.tv_sec / time_unit;

    while (!terminate) {
        gettimeofday (&tv, NULL);
        // tm = localtime (&tv.tv_sec);
        tm = localtime_r (&tv.tv_sec, &tmvalue);
        prev_day_of_year = day_of_year;
        day_of_year = tm->tm_yday;

        if ((current_unit = tv.tv_sec / time_unit) == last_unit) {
//            usleep ((59 - (tv.tv_sec % 60)) * 1000000 + 1000000 - tv.tv_usec);
            usleep ((time_unit - (tv.tv_sec % time_unit) - 1) * 1000000 + 1000000 - tv.tv_usec);
        } else {
            last_unit = current_unit;

            if (logger->isEnable (log_debug)) {
                logger->debug (__FILE__, __LINE__, "Minute timer: %02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
            }
            (*func) (&tv, tm);
        }
    }
}

static struct minute_timer_t instance = {
    .start = start_minute_timer,
    .get_day_of_year = get_day_of_year,
    .get_prev_day_of_year = get_prev_day_of_year,
    .terminate = terminate_minute_timer,
};

struct minute_timer_t *new_minite_timer() {
    logger = get_application_context()->get_logger();
    return &instance;
}
