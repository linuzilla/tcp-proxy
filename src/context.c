//
// Created by saber on 5/11/22.
//

#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include "context.h"
#include "hash_map.h"
#include "logger.h"
#include "exception.h"

struct entry_t {
    const context_aware_data_t *context;
    bool initialized;
    struct entry_t *next;
};

struct error_message_t {
    int errcode;
    const char * const message;
};

static pthread_once_t once_control = PTHREAD_ONCE_INIT;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct logger_t *logger = &excalibur_common_logger;
static struct hash_map_t *hashMap;
static struct hash_map_t *variableMap;
static struct entry_t *entries = NULL;

static struct error_message_t errorMessages[] = {
    {
        .errcode = CONTEXT_ERROR_MAGIC_NUMBER,
        .message = "Magic number mismatch, not a proper object",
    },
    {
        .errcode = CONTEXT_ERROR_VERSION_NUMBER,
        .message = "Version number mismatch, not a proper object or version not compatible",
    },
    {
        .errcode = CONTEXT_ERROR_KEY_EXISTS,
        .message = "Key exists",
    },
};

static const char * const error_message (const int error_code) {
    static const int errCount = sizeof (errorMessages) / sizeof (struct error_message_t);
    int i;

    for (i = 0; i < errCount; i++) {
        if (errorMessages[i].errcode == error_code) {
            return errorMessages[i].message;
        }
    }
    return "Unknown error";
}

static bool wiring (const bool casual) {
    struct entry_t *ptr = entries;
    bool done = true;
    int wiring_count = 0;

    do {
        done = true;
        wiring_count = 0;

        while (ptr != NULL) {
            if (!ptr->initialized) {
                bool check_entry_success = true;

                if (ptr->context->depends_on != NULL) {
                    int i, count;
                    const char **items = ptr->context->depends_on (&count);

                    for (i = 0; i < count; i++) {
                        const struct entry_t *entry = hashMap->get (hashMap, * (items + i));

                        if (entry != NULL) {
                            if (!entry->initialized) {
                                check_entry_success = false;
                                break;
                            }
                        } else if (!casual) {
                            logger->fatal (__FILE__, __LINE__, "autowiring: %s not found", * (items + i));
                        }
                    }
                }

                if (check_entry_success) {
                    if (ptr->context->post_construct != NULL) {
                        ptr->context->post_construct();
                    }
                    ptr->initialized = true;
                    wiring_count++;
                } else {
                    done = false;
                }
            }
            ptr = ptr->next;
        }
    } while (wiring_count > 0);

    return done;
}

static bool auto_wiring (void) {
    return wiring (false);
}

static struct entry_t *new_entry (const context_aware_data_t *context) {
    struct entry_t *entry = malloc (sizeof (struct entry_t));
    entry->context = context;
    entry->initialized = false;
    entry->next = entries;
    entries = entry;

    wiring (true);
    return entry;
}

static int populate (const void *unknown) {
    const context_aware_data_t *context = unknown;

    if (context->header.magic != CONTEXT_MAGIC_NUMBER) {
        logger->error (__FILE__, __LINE__, "%u vs %u", context->header.magic, CONTEXT_MAGIC_NUMBER);

        return CONTEXT_ERROR_MAGIC_NUMBER;
    } else if (context->header.version_major != CONTEXT_MAJOR_VERSION && context->header.version_minor != CONTEXT_MINOR_VERSION) {
        return CONTEXT_ERROR_VERSION_NUMBER;
    }

    if (hashMap->put_if_absent (hashMap, context->name(), new_entry (context)) != NULL) {
        return CONTEXT_ERROR_KEY_EXISTS;
    }
    return 0;
}

static const void * get_bean (const char *const name) {
    const struct entry_t * entry = hashMap->get (hashMap, name);

    if (entry != NULL && entry->initialized) {
        return entry->context;
    }
    return NULL;
}

static bool set_pointer (const char *const name, const void *value) {
    return variableMap->put_if_absent (variableMap, name, value) == NULL;
}

static const void * get_pointer (const char *const name) {
    return variableMap->get (variableMap, name);
}

static struct logger_t *get_logger (void) {
    return logger;
}

static struct application_context_t instance = {
    .populate = populate,
    .error_message = error_message,
    .get_logger = get_logger,
    .auto_wiring = auto_wiring,
    .get_bean = get_bean,
    .set_pointer = set_pointer,
    .get_pointer = get_pointer,
};

static void initial_once (void) {
    hashMap = new_hash_map (97, NULL);
    variableMap = new_hash_map (97, NULL);
}

struct application_context_t *get_application_context() {
    pthread_mutex_lock (&mutex);
    pthread_once (&once_control, initial_once);
    pthread_mutex_unlock (&mutex);

    return &instance;
}