//
// Created by Mac Liu on 12/22/21.
//

#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include "global_vars.h"
#include "logger.h"
#include "auto_blacklist.h"
#include "context.h"


static int hash_buffer_size = 0;
static int frequency_in_seconds = 10;

struct ip_access_entry_header_t {
    struct ip_access_entry_t *entries;
    pthread_mutex_t mutex;
};

static struct logger_t *logger = &excalibur_common_logger;
static struct ip_access_entry_header_t *ip_access_buffer;
static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct ip_access_entry_t *release_pool = NULL;
static pthread_mutex_t expiring_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t expiring_cond = PTHREAD_COND_INITIALIZER;
static volatile bool terminate = false;
static pthread_t expiring_thread;

static struct ip_access_entry_t *allocate_new_entry() {
    struct ip_access_entry_t *entry = NULL;

    pthread_mutex_lock (&pool_mutex);
    if (release_pool != NULL) {
        entry = release_pool;
        release_pool = release_pool->next;
    } else {
        entry = malloc (sizeof (struct ip_access_entry_t));
    }
    pthread_mutex_unlock (&pool_mutex);

    return entry;
}

static void free_entry (struct ip_access_entry_t *entry) {
    pthread_mutex_lock (&pool_mutex);
    entry->next = release_pool;
    release_pool = entry;
    pthread_mutex_unlock (&pool_mutex);
}

static int current_time_index (int *slot_index) {
    time_t current_time = time (NULL);

    int simplified = current_time / frequency_in_seconds;

    if (slot_index != NULL) {
        *slot_index = simplified;
    }
    return simplified % RESERVED_ENTRY;
}

static char *ip_address_of (struct ip_access_entry_t *entry) {
    struct in_addr address = {.s_addr = htonl (entry->ipaddr)};
    return inet_ntoa (address);
}

static void clear_outdated_data (struct ip_access_entry_t *entry, int index, int slot_index) {
    int i, j, s;

    for (i = index, s = slot_index, j = 0;
            j < RESERVED_ENTRY; i = (i + RESERVED_ENTRY - 1) % RESERVED_ENTRY, s--, j++) {
        if (entry->access_count[i].slot_index != s) {
            entry->access_count[i].slot_index = s;

            if (entry->access_count[i].counter > 0) {
                entry->counter -= entry->access_count[i].counter;
                entry->access_count[i].counter = 0;

                if (logger->isEnable (log_trace)) {
                    logger->trace (__FILE__, __LINE__,
                                   "[Auto Blacklist] clear outdated data (ip: %s, slot_index: %d, index: %d, count: %d, entry total: %d)",
                                   ip_address_of (entry),
                                   s,
                                   i,
                                   entry->access_count[i].counter,
                                   entry->counter);
                }
            }
        }
    }
}

static struct ip_access_entry_t * find_and_increase (struct in_addr *address) {
    uint32_t ip = htonl (address->s_addr);

    int hash_index = ip % hash_buffer_size;

    struct ip_access_entry_header_t *header = ip_access_buffer + hash_index;
    struct ip_access_entry_t *entry = header->entries;

    int slot_index = 0;
    int index = current_time_index (&slot_index);
    int i;

    pthread_mutex_lock (&header->mutex);
    while (entry != NULL) {
        if (entry->ipaddr == ip) {
            break;
        } else {
            entry = entry->next;
        }
    }

    if (entry == NULL) {
        entry = allocate_new_entry();
        entry->ipaddr = ip;
        entry->counter = 0;
        entry->success_counter = 0;

        for (i = 0; i < RESERVED_ENTRY; i++) {
            entry->access_count[i].counter = 0;
            entry->access_count[i].slot_index = 0;
        }

        entry->next = header->entries;
        header->entries = entry;
        gettimeofday (&entry->log_time, NULL);
    }

    if (entry->access_count[index].slot_index != slot_index) {
        clear_outdated_data (entry, index, slot_index);
    }
    pthread_mutex_unlock (&header->mutex);

    entry->access_count[index].counter++;
    entry->counter++;

    if (logger->isEnable (log_trace)) {
        logger->trace (__FILE__, __LINE__,
                       "[Auto Blacklist] increase access count (ip: %s, hash_index: %d, slot_index: %d, index: %d, count: %d, entry total: %d)",
                       inet_ntoa (*address),
                       hash_index,
                       slot_index,
                       index,
                       entry->access_count[index].counter,
                       entry->counter);
    }

    return entry;
}

static int expiring() {
    static int recent_index = -1;

    int k, release_counter = 0;

    int slot_index = 0;
    int index = current_time_index (&slot_index);

    if (index != recent_index) {
        struct ip_access_entry_t *free_list = NULL;
        int total_entries = 0;
        int longest_chain = 0;
        int number_of_empty_chain = 0;

        for (k = 0; k < hash_buffer_size; k++) {
            struct ip_access_entry_header_t *header = &ip_access_buffer[k];

            pthread_mutex_lock (&header->mutex);

            struct ip_access_entry_t *entry = header->entries;
            struct ip_access_entry_t **prev = &header->entries;
            int size_of_chain = 0;

            while (entry != NULL) {
                clear_outdated_data (entry, index, slot_index);

                if (entry->counter == 0) {
                    if (logger->isEnable (log_info)) {
                        logger->info (__FILE__, __LINE__, "[Expiring Thread] Free entry: %s", ip_address_of (entry));
                    }

                    *prev = entry->next;
                    entry->next = free_list;
                    free_list = entry;
                    entry = *prev;
                    release_counter++;
                } else {
                    prev = &entry->next;
                    entry = entry->next;
                    total_entries++;
                    size_of_chain++;
                }
            }

            pthread_mutex_unlock (&header->mutex);

            while ((entry = free_list) != NULL) {
                free_list = free_list->next;
                free_entry (entry);
            }
            longest_chain = longest_chain >= size_of_chain ? longest_chain : size_of_chain;

            if (size_of_chain == 0) {
                number_of_empty_chain++;
            }
        }

        recent_index = index;

        logger->notice (__FILE__, __LINE__, "[Expiring Thread] expire %d entries, %d entries left, longest chain: %d entries, empty chain: %d",
                        release_counter, total_entries, longest_chain, number_of_empty_chain);
    }

    return release_counter;
}

static void *expiring_main() {
    logger->notice (__FILE__, __LINE__, "[Expiring Thread] Started");
    while (!terminate) {
        pthread_mutex_lock (&expiring_mutex);
        pthread_cond_wait (&expiring_cond, &expiring_mutex);

        logger->debug (__FILE__, __LINE__, "[Expiring Thread] begin");
        expiring();
        logger->debug (__FILE__, __LINE__, "[Expiring Thread] done");

        pthread_mutex_unlock (&expiring_mutex);
    }
    logger->notice (__FILE__, __LINE__, "[Expiring Thread] Ended");
    pthread_exit (NULL);
}

static void wakeup() {
    logger->trace (__FILE__, __LINE__, "[Expiring Thread] Try to Wakeup");
    pthread_mutex_lock (&expiring_mutex);
    pthread_cond_signal (&expiring_cond);
    logger->debug (__FILE__, __LINE__, "[Expiring Thread] Wakeup");
    pthread_mutex_unlock (&expiring_mutex);
}

static void terminate_thread() {
    terminate = true;

    wakeup();
}

static void for_each (void (*callback) (struct ip_access_entry_t *)) {
    int i;

    for (i = 0; i < hash_buffer_size; i++) {
        struct ip_access_entry_header_t *header = &ip_access_buffer[i];

        pthread_mutex_lock (&header->mutex);

        struct ip_access_entry_t *entry = header->entries;

        while (entry != NULL) {
            if (entry->counter > 0) {
                callback (entry);
            }
            entry = entry->next;
        }

        pthread_mutex_unlock (&header->mutex);
    }
}

static const char *const context_name (void) {
    const static char *const name = AUTO_BLACKLIST_DEFAULT_CONTEXT_NAME;
    return name;
}

static void post_construct (void) {
    logger->trace (__FILE__, __LINE__, "%s:%d %s", __FILE__, __LINE__, __FUNCTION__ );
}

static struct auto_blacklist_service_t instance = {
    .context = {
        .header = {
            .magic = CONTEXT_MAGIC_NUMBER,
            .version_major = CONTEXT_MAJOR_VERSION,
            .version_minor = CONTEXT_MINOR_VERSION,
        },
        .name = context_name,
        .post_construct = post_construct,
        .depends_on = NULL,
    },
    .find_and_increase = find_and_increase,
    .terminate = terminate_thread,
    .expiring = wakeup,
    .for_each = for_each,
};

static bool initialized = false;

struct auto_blacklist_service_t *get_auto_blacklist_service() {
    return initialized ? &instance : NULL;
}

struct auto_blacklist_service_t *new_auto_blacklist_service (const int hash_size, const int monitor_period) {
    logger = get_application_context()->get_logger();

    if (!initialized) {
        hash_buffer_size = hash_size;
        frequency_in_seconds = monitor_period / RESERVED_ENTRY;
        ip_access_buffer = malloc (hash_buffer_size * sizeof (struct ip_access_entry_header_t));
        int i;

        for (i = 0; i < hash_buffer_size; i++) {
            ip_access_buffer[i].entries = NULL;
            pthread_mutex_init (&ip_access_buffer[i].mutex, NULL);
        }

        pthread_create (&expiring_thread, NULL, expiring_main, NULL);

        initialized = true;
    }
    return &instance;
}