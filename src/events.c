#include <sys/epoll.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "events.h"
#include "logger.h"

#define MAX_EVENTS 1024

static struct logger_t *logger;

static struct event_loop_data_t {
    int epollfd;
    struct epoll_event events[MAX_EVENTS];
    int fds[MAX_EVENTS];
    void *args[MAX_EVENTS];
    int max_events;
    int num_of_events;

    void (*handlers[MAX_EVENTS]) (const int fd, void *args);
} singleton_data, *data;

static struct event_loop_t singleton, *self;


static int find_first_free() {
    int i = 0;

    for (i = 0; i < data->max_events; i++) {
        if (data->fds[i] == -1) {
            data->num_of_events++;
            return i;
        }
    }
    if (data->max_events < MAX_EVENTS) {
        i = data->max_events;
        data->max_events++;
        data->num_of_events++;
        return i;
    }
    return -1;
}

static bool ev_remove_event (int index) {
    if (index >= 0 && index < data->max_events) {
        data->fds[index] = -1;
        data->num_of_events--;

        while (data->max_events > 0 && data->fds[data->max_events - 1] < 0) {
            data->max_events--;
        }
        return true;
    }
    return false;
}

static int ev_add_event (int fd, void (*handler) (const int, void *), void *args) {
    int index = find_first_free();

    if (index >= 0) {
//        fprintf (stderr, "register event fd=%d, index=%d\n", fd, index);
        struct epoll_event ev = {
            .events = EPOLLIN,
            .data.fd = fd
        };

        if (epoll_ctl (data->epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
            perror ("epoll_ctl: listen_sock");
            return -1;
        }

        data->fds[index] = fd;
        data->handlers[index] = handler;
        data->args[index] = args;

        return index;
    } else {
        fprintf (stderr, "reach max file descriptors\n");
        return -2;
    }
}

static int ev_looping() {
//    fprintf (stderr, "Event loop (%d)\n", data->max_events);
    int nfds = epoll_wait (data->epollfd, data->events, data->max_events, -1);
    int i, n;

    if (nfds == -1) {
        logger->error (__FILE__, __LINE__, "epoll_wait: %s", strerror (errno));
//        return -1;
        return 0;
    }

    for (n = 0; n < nfds; ++n) {
//        fprintf (stderr, "%d/%d\n", n, nfds);
        for (i = 0; i < data->max_events; i++) {
            if (data->events[n].data.fd == data->fds[i]) {
                data->handlers[i] (data->fds[i], data->args[i]);
                return 0;
            }
        }
    }
    return 0;
}

static int ev_count() {
    return data->num_of_events;
}

struct event_loop_t *new_event_loop (struct logger_t *newLogger) {
    logger = newLogger;

    if (self == NULL) {
        self = &singleton;
        data = &singleton_data;

        data->epollfd = epoll_create1 (0);
        data->max_events = 0;
        data->num_of_events = 0;

        if (data->epollfd == -1) {
            perror ("epoll_create1");
            return NULL;
        }

        self->add_event = ev_add_event;
        self->remove_event = ev_remove_event;
        self->looping = ev_looping;
        self->count = ev_count;
    }
    return self;
}

