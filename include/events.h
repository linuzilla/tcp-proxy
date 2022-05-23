//
// Created by Mac Liu on 8/16/21.
//

#ifndef TCP_PROXY_EVENTS_H
#define TCP_PROXY_EVENTS_H

#include <stdbool.h>

struct logger_t;

struct event_loop_t {
    bool (*remove_event) (int index);
    int (*add_event) (int fd, void (*handler) (const int, void *), void *data);
    int (*looping)();
    int (*count)();
};

struct event_loop_t *new_event_loop (struct logger_t *newLogger);

#endif
