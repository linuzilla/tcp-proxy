//
// Created by saber on 5/5/22.
//

#ifndef PACKET_ANALYZER_H
#define PACKET_ANALYZER_H

#include <sys/types.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "logger.h"
#include "context.h"
#include "proxying.h"

#define PACKET_ANALYZER_MODULE_NAME "pluggablePacketAnalyzer"
#define PACKET_ANALYZER_DEFAULT_CONTEXT_NAME "packet-analyzer"

#define SYSTEM_CONF_VARIABLE_PACKET_ANALYZER_PLUGIN "packet-analyzer-plugin"
#define DEFAULT_PACKET_ANALYZER_PLUGIN_LOCATION  "/usr/local/lib/tcp-proxy/libpkanalyzer.so"

struct pluggable_packet_analyzer_t {
    void (*init) (struct application_context_t *context);
    void* (*allocate) (void);
    void (*release) (void *data);
    uint64_t (*analyze) (struct connection_info *info, bool fromClient, char *buffer, const ssize_t len);
};

struct packet_analyzer_t {
    context_aware_data_t context;
    int (*load_packet_analyzer) (bool boot, const char *module_name);
    bool (*load) (const char *file, const char *symbol);
    bool (*unload) (void);
    bool (*set_enable) (const bool on_off);
    void (*set_safe_mode) (const bool on_off);
    bool (*get_safe_mode) (void);
    void* (*allocate) (void);
    void (*release) (void *data);
    uint64_t (*analyze_packet) (struct connection_info *info, bool fromClient, char *buffer, const ssize_t len);
};

struct packet_analyzer_t *init_packet_analyzer (void);

#endif //PACKET_ANALYZER_H
