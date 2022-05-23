//
// Created by Mac on 12/24/2021.
//

#ifndef TCP_PROXY_GLOBAL_VARS_H
#define TCP_PROXY_GLOBAL_VARS_H

#include <pthread.h>
#include <stdbool.h>
#include "logger.h"
#include "min_timer.h"
#include "auto_blacklist.h"
#include "packet_analyzer.h"

#ifndef APP_VERSION
#define APP_VERSION "0.0.1"
#endif

struct global_vars_t {
    pthread_t *main_thread;
    pthread_t *command_thread;
    time_t *app_boot_time;
};

extern struct global_vars_t global_vars;
//
////extern struct auto_blacklist_service_t * blacklistService;
////extern struct logger_t *logger;
////extern struct packet_analyzer_t *packetAnalyzer;
//
////extern pthread_t main_thread;
////extern pthread_t command_thread;
////extern time_t app_boot_time;

#endif //TCP_PROXY_GLOBAL_VARS_H
