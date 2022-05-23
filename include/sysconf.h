#ifndef __SYSCONF_H__
#define __SYSCONF_H__

#include <stdio.h>
#include <stdbool.h>
#include <netinet/in.h>
#include "context.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_CONFIG_DEFAULT_CONTEXT_NAME "system-config"

#define CONF_VAR_IP_MAC_QUERIES "ip-mac-queries"
#define CONF_VAR_MYSQL_SERVER "mysql-server"
#define CONF_VAR_MYSQL_ACCOUNT "mysql-account"
#define CONF_VAR_MYSQL_PASSWORD "mysql-passwd"
#define CONF_VAR_MYSQL_DATABASE "mysql-database"

enum entry_type_enum {
    IntegerList,
    StringList,
    IntegerValue,
    StringValue,
    BooleanValue
};

//struct system_config_data_t {
//    char *listen_interface;
//    struct in_addr arpguard_network;
//    struct in_addr arpguard_netmask;
//    int with_arpguard_network;
//};

struct system_config_t {
    context_aware_data_t context;

    void (*addentry_integer) (const char *entry, const char *value);

    void * (*addentry_string) (const char *entry, const char *value);

    void (*addentry_ip) (const char *entry, const char *value);

    void (*addentry_mac) (const char *entry, const char *value);

    void (*addentry_flag_on) (const char *entry);

    void (*addentry_flag_off) (const char *entry);

//    void (*set_listen_interface) (const char *interface);

//    void (*set_network_netmask) (const char *network, const char *netmask);

    void (*add_prepared_list) (const char *entry);

    void (*set_list_as_integer) (void);

    void (*set_list_as_string) (void);

    void (*list_append_int) (char *entry);

    void (*list_append_str) (char *entry);

    const char * const (*str) (const char *key);

    const char * const (*str_or_default) (const char *key, const char *defaultValue);

    int (*integer) (const char *key);

    int * (*integer_list) (const char *key, int *sz);

    char ** (*string_list) (const char *key, int *sz);

    int (*int_or_default) (const char *key, const int defaultValue);

    char * (*first_key) (void);

    char * (*next_key) (void);

    enum entry_type_enum (*data_type) (const char *key);

    volatile bool (*terminated) (void);

    void (*terminate) (void);

    struct system_config_data_t * (*get_config_data) (void);

};

struct system_config_t *new_system_config (const char *filename);

extern struct system_config_t *system_config;
extern FILE *yyin;

#ifdef __cplusplus
}
#endif

#endif
