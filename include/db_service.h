//
// Created by saber on 11/30/21.
//

#ifndef TCP_PROXY_DB_SERVICE_H
#define TCP_PROXY_DB_SERVICE_H

#include <sys/types.h>
#include <stdbool.h>
#include <time.h>
#include "logger.h"
#include "context.h"

#define DATABASE_SERVICE_DEFAULT_CONTEXT_NAME "database-service"

struct system_config_t;
struct connection_info;

struct db_proxy_request_t {
    char *account;
    int sn;
    int channel;
};

struct database_service_t {
    context_aware_data_t context;
    struct db_proxy_request_t * (*check_available) (const char *remote_ip);
    void (*connection_close) (const int sn, const ssize_t bytes, const int count, const bool idle);
    int (*connection_established) (const int sn, const char *account, const char *ipaddr);
    void (*connection_not_allowed) (const char *ipaddr);
    int (*connection_blacklisted) (const char *ipaddr);
    int (*check_vip) (const char *ipaddr);
    int (*add_ip_to_auto_blacklist) (const char *ipaddr);
    void (*close_idle) (const struct timeval *, const struct tm *tm);
    void (*done) (void);
    void (*set_logger) (struct logger_t *new_logger);
    bool (*fail_guessing) (const char * const ip_address);
    int (*add_kms_details) (const struct connection_info *info,
                            const char * const workstation,
                            const int major_version,
                            const int minor_version,
                            const char * const app_id,
                            const char * const kms_id,
                            const char * const client_machine_id,
                            const int remaining_min);
    int (*update_machine_owner) (const struct connection_info *info, const char * const client_machine_id);
    void (*reload_product_names) (void);
    const char * const (*get_product_name) (const char *const app_id, const char * const kms_id);
};

struct database_service_t *new_database_service (struct system_config_t *sysconf);

#endif //TCP_PROXY_DB_SERVICE_H
