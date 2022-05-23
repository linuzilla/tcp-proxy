//
// Created by Mac Liu on 12/22/21.
//

#ifndef TCP_PROXY_AUTO_BLACKLIST_H
#define TCP_PROXY_AUTO_BLACKLIST_H

#include <time.h>
#include <stdbool.h>
#include <netinet/in.h>
#include "context.h"

#define RESERVED_ENTRY 12
#define AUTO_BLACKLIST_DEFAULT_CONTEXT_NAME "auto-blacklist"

struct access_entry_t {
    int counter;
    int slot_index;
};

struct ip_access_entry_t {
    uint32_t ipaddr;
    struct access_entry_t access_count[RESERVED_ENTRY];
    time_t recent;
    int counter;
    int success_counter;
    struct timeval log_time;
    struct ip_access_entry_t *next;
};

struct auto_blacklist_service_t {
    context_aware_data_t context;
    struct ip_access_entry_t * (*find_and_increase) (struct in_addr *address);
    void (*terminate) (void);
    void (*expiring) (void);
    void (*for_each) (void (*callback) (struct ip_access_entry_t *));
};

extern struct auto_blacklist_service_t *new_auto_blacklist_service (const int hash_size, const int monitor_period);
extern struct auto_blacklist_service_t *get_auto_blacklist_service();

#endif //TCP_PROXY_AUTO_BLACKLIST_H
