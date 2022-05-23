//
// Created by saber on 5/11/22.
//

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "hash_map.h"

struct hash_bucket_t {
    const char *key;
    const void *value;
    struct hash_bucket_t **prev;
    struct hash_bucket_t *next;
};

struct hash_map_data_t {
    pthread_rwlock_t rwlock;
    int number_of_entries;
    int buckets_size;
    struct hash_bucket_t **buckets;
    int (*hashing_function) (const char *const key);
};

static int default_hashing_function (const char *const key) {
    int key_length = strlen (key);
    int i, code = 0;

    for (i = 0; i < key_length; i++) {
        code = (code * 13 + (int) key[i]) % 31636373;
    }

    return code;
}

static struct hash_bucket_t ** find_head (struct hash_map_data_t *data, const char *const key) {
    int i = data->hashing_function (key) % data->buckets_size;
    return (data->buckets + i);
}

static struct hash_bucket_t * find_data (struct hash_map_data_t *data, struct hash_bucket_t **head, const char *const key) {
    if (head != NULL) {
        struct hash_bucket_t **ptr = head;

        while (*ptr != NULL) {
            if (strcmp (key, (*ptr)->key) == 0) {
                return *ptr;
            }
            ptr = & (*ptr)->next;
        }
    }
    return NULL;
}

static bool exists (struct hash_map_t *self, const char *const key) {
    struct hash_map_data_t *data = self->data;

    pthread_rwlock_rdlock (&data->rwlock);
    bool is_exists = find_data (data, find_head (data, key), key) != NULL;
    pthread_rwlock_unlock (&data->rwlock);

    return is_exists;
}

static const void * get_by_key (struct hash_map_t *self, const char *const key) {
    struct hash_map_data_t *data = self->data;
    const void *value = NULL;

    pthread_rwlock_rdlock (&data->rwlock);

    struct hash_bucket_t *ptr = find_data (data, find_head (data, key), key);
    if (ptr != NULL) {
        value = ptr->value;
    }

    pthread_rwlock_unlock (&data->rwlock);
    return value;
}

static const void * put_if_absent (struct hash_map_t *self, const char *const key, const void *value) {
    struct hash_map_data_t *data = self->data;
    const void *origin = NULL;

    pthread_rwlock_wrlock (&data->rwlock);

    struct hash_bucket_t **head = find_head (data, key);
    struct hash_bucket_t *ptr = find_data (data, head, key);

    if (ptr != NULL) {
        origin = ptr->value;
    } else {
        if ((ptr = malloc (sizeof (struct hash_bucket_t))) != NULL) {
            ptr->key = key;
            ptr->value = value;
            ptr->next = *head;
            ptr->prev = head;
            *head = ptr;
        }
    }

    pthread_rwlock_unlock (&data->rwlock);
    return origin;
}

static const void * overwrite_put (struct hash_map_t *self, const char *const key, const void *value) {
    struct hash_map_data_t *data = self->data;
    const void *origin = NULL;

    pthread_rwlock_wrlock (&data->rwlock);

    struct hash_bucket_t **head = find_head (data, key);
    struct hash_bucket_t *ptr = find_data (data, head, key);

    if (ptr != NULL) {
        origin = ptr->value;
        ptr->value = value;
    } else {
        if ((ptr = malloc (sizeof (struct hash_bucket_t))) != NULL) {
            ptr->key = key;
            ptr->value = value;
            ptr->next = *head;
            ptr->prev = head;
            *head = ptr;
        }
    }

    pthread_rwlock_unlock (&data->rwlock);
    return origin;
}

static const void * remove (struct hash_map_t *self, const char *const key) {
    struct hash_map_data_t *data = self->data;
    const void *value = NULL;

    pthread_rwlock_wrlock (&data->rwlock);
    struct hash_bucket_t *ptr = find_data (data, find_head (data, key), key);

    if (ptr != NULL) {
        (*ptr->prev)->next = ptr->next;
        ptr->next->prev = ptr->prev;
        value = ptr->value;
        free (ptr);
    }
    pthread_rwlock_unlock (&data->rwlock);
    return value;
}

static void clear (struct hash_map_t *self) {
    struct hash_map_data_t *data = self->data;
    int i;

    pthread_rwlock_wrlock (&data->rwlock);
    for (i = 0; i < data->buckets_size; i++) {
        if (* (data->buckets + i) != NULL) {
            struct hash_bucket_t **ptr = data->buckets + i;

            while (*ptr != NULL) {
                struct hash_bucket_t *next = (*ptr)->next;

                free (*ptr);
                *ptr = next;
            }

            free (* (data->buckets + i));
        }
    }
    pthread_rwlock_unlock (&data->rwlock);
}

static void dispose (struct hash_map_t *self) {
    if (self != NULL) {
        if (self->data != NULL) {
            struct hash_map_data_t *data = self->data;

            if (data->buckets != NULL) {
                clear (self);
                free (data->buckets);
            }
            free (self->data);
        }
        free (self);
    }
}

static struct hash_map_t instance = {
    .exists = exists,
    .get = get_by_key,
    .put_if_absent = put_if_absent,
    .put = overwrite_put,
    .remove = remove,
    .dispose = dispose,
};

struct hash_map_t *new_hash_map (const int buckets_size, int (*hashing_function) (const char *const key)) {
    struct hash_map_t *self = malloc (sizeof (struct hash_map_t));

    if (self != NULL) {
        memcpy (self, &instance, sizeof (struct hash_map_t)) ;

        if ((self->data = malloc (sizeof (struct hash_map_data_t))) == NULL) {
            self->dispose (self);
        } else {
            struct hash_map_data_t *data = self->data;

            data->number_of_entries = 0;
            data->buckets_size = buckets_size;
            data->buckets = calloc (buckets_size, sizeof (struct hash_bucket_t *));
            data->hashing_function = hashing_function != NULL ? hashing_function : default_hashing_function;
            pthread_rwlock_init (&data->rwlock, NULL);

            if (data->buckets == NULL) {
                self->dispose (self);
            } else {
                int i;
                for (i = 0; i < buckets_size; i++) {
                    * (data->buckets + i) = NULL;
                }
            }

        }
    }

    return self;
}