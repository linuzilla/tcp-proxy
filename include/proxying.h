//
// Created by saber on 11/30/21.
//

#ifndef TCP_PROXY_PROXYING_H
#define TCP_PROXY_PROXYING_H

#include <pthread.h>
#include <time.h>
#include "context.h"
#include "db_service.h"

#define PROXYING_SERVICE_DEFAULT_CONTEXT_NAME "proxying-service"

struct connection_info {
    int64_t connection_id;
    int client_fd;
    int server_fd;
    int client_handle;
    int server_handle;
    int requestCount;
    int responseCount;
    uint32_t insert_id;
    uint32_t nth_user;
    ssize_t bytesSent;
    ssize_t bytesReceived;
    struct timeval started;
    struct timeval recent;
    bool in_chain;
    struct db_proxy_request_t *request_in_db;
    char *remote_ip;
    pthread_mutex_t mutex;
    struct connection_info *prev;
    struct connection_info *next;
    int attempts;
    void *packet_analyzer_data;
};

struct proxying_service_t {
    context_aware_data_t context;
    int (*start_proxying) (pthread_t *thread);
    void (*clean_idle_connection) (const struct timeval *tv, double timeout);
    int (*get_default_channel) (void);
    int (*get_fallback_channel) (void);
    int (*set_default_channel) (const int channel);
    int (*set_fallback_channel) (const int channel);
};

struct proxying_service_t * init_proxying_service ();

//extern void * proxy_main (void *);
//extern void clean_idle_connections (const struct timeval *tv, double timeout);

#endif //TCP_PROXY_PROXYING_H
