//
// Created by saber on 5/11/22.
//

#ifndef TCP_PROXY_HASH_MAP_H
#define TCP_PROXY_HASH_MAP_H

#include <stdbool.h>
struct hash_map_t {
    void *data;
    bool (*exists) (struct hash_map_t *self, const char *const key);
    const void * (*get) (struct hash_map_t *self,  const char *const key);
    /**
     *
     * @param self
     * @param key
     * @param value
     * @return NULL (success), Non-Null (pointer to current value)
     */
    const void * (*put_if_absent) (struct hash_map_t *self, const char *const key, const void *value);
    const void * (*put) (struct hash_map_t *self, const char *const key, const void *value);
    const void * (*remove) (struct hash_map_t *self, const char *const key);
    void (*dispose) (struct hash_map_t *self);
};

extern struct hash_map_t *new_hash_map (const int buckets_size, int (*hashing_function) (const char *const key));

#endif //TCP_PROXY_HASH_MAP_H
