//
// Created by saber on 5/11/22.
//

#ifndef TCP_PROXY_CONTEXT_H
#define TCP_PROXY_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>

struct logger_t;

#define CONTEXT_MAGIC_NUMBER 1012315172
#define CONTEXT_MAJOR_VERSION 1
#define CONTEXT_MINOR_VERSION 0

#define CONTEXT_ERROR_MAGIC_NUMBER 1
#define CONTEXT_ERROR_VERSION_NUMBER 2
#define CONTEXT_ERROR_KEY_EXISTS 3



typedef struct {
    uint32_t magic;
    uint16_t version_major;
    uint16_t version_minor;
} __attribute__ ((packed)) context_data_header_t;

typedef struct {
    context_data_header_t header;
    const char *const (*name) (void);
    const char ** (*depends_on) (int *number);
    void (*post_construct) (void);
} __attribute__ ((packed)) context_aware_data_t;

struct my_example {
    context_aware_data_t context;
};

struct application_context_t {
    int (*populate) (const void *context);
    const char * const (*error_message) (const int error_code);
    struct logger_t * (*get_logger) (void);
    bool (*auto_wiring) (void);
    const void * (*get_bean) (const char *const name);
    bool (*set_pointer) (const char *const name, const void *value);
    const void * (*get_pointer) (const char *const name);
};

struct application_context_t *get_application_context();
#endif //TCP_PROXY_CONTEXT_H
