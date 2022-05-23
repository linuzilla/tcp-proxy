//
// Created by Mac Liu on 11/30/21.
//

#include <sys/time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include "sysconf.h"
#include "db_service.h"
#include "db_mysql.h"
#include "db_xsql.h"
#include "db_conninfo.h"
#include "logger.h"
#include "utils.h"
#include "exception.h"
#include "proxying.h"
#include "hash_map.h"

static const double database_timeout = 300.;

static struct db_xsql_t *db;
static struct db_xsql_data_t *db_data;
static struct db_connection_info_t db_connection_info;
static struct system_config_t *system_conf = NULL;
static pthread_mutex_t connection_mutex = PTHREAD_MUTEX_INITIALIZER;
// static pthread_mutex_t reconnect_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timeval recent_use_time;
static struct timeval connection_time;
static bool connected;
static bool enabled;
static time_t start_time;
static struct logger_t *logger;

enum query_name_enum {
    SQL_NAME_NOT_FOUND,
    SQL_CHECK_AVAILABLE,
    SQL_CONNECTION_CLOSE,
    SQL_LAST_INSERT_ID,
    SQL_CONNECTION_NOT_ALLOWED,
    SQL_CHECK_VIP,
    SQL_CONNECTION_ESTABLISHED,
    SQL_CONNECTION_BEGIN,
    SQL_BLACKLIST,
    SQL_ADD_TO_BLACKLIST,
    SQL_ADD_DETAILS,
    SQL_ADD_MACHINE_OWNER,
    SQL_UPDATE_MACHINE_ACCESS,
    SQL_CALL_FAILURE_GUESSING,
    SQL_ALL_PRODUCT_NAMES,
};

struct queries_and_statements {
    enum query_name_enum index;
    const char *query_name;
    const char *query;
    struct db_xsql_stmt_t *stmt;
};

static struct queries_and_statements stmt_not_found = {
    .index = SQL_CONNECTION_NOT_ALLOWED,
    .query = "SELECT 1",
    .stmt = NULL,
};

static struct queries_and_statements stmt_holder[] = {
    {
        .index = SQL_CHECK_AVAILABLE,
        .query_name = "sql-check-available",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_CONNECTION_CLOSE,
        .query_name = "sql-connection-close",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_LAST_INSERT_ID,
        .query_name = NULL,
        .query = "SELECT LAST_INSERT_ID()",
        .stmt = NULL,
    },
    {
        .index = SQL_CONNECTION_NOT_ALLOWED,
        .query_name = "sql-connection-not-allowed",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_CHECK_VIP,
        .query_name = "sql-check-vip",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_CONNECTION_ESTABLISHED,
        .query_name = "sql-connection-established",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_CONNECTION_BEGIN,
        .query_name = "sql-connection-begin",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_BLACKLIST,
        .query_name = "sql-blacklist",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_ADD_TO_BLACKLIST,
        .query_name = "sql-add-to-blacklist",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_ADD_DETAILS,
        .query_name = "sql-add-details",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_ADD_MACHINE_OWNER,
        .query_name = "sql-add-machine-owner",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_UPDATE_MACHINE_ACCESS,
        .query_name = "sql-update-machine-access",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_CALL_FAILURE_GUESSING,
        .query_name = "sql-call-failure-guessing",
        .query = NULL,
        .stmt = NULL,
    },
    {
        .index = SQL_ALL_PRODUCT_NAMES,
        .query_name = "sql-all-product-names",
        .query = NULL,
        .stmt = NULL,
    },
};

static bool connect_to_database (bool re_connect) {
    pthread_mutex_lock (&connection_mutex);

    if (enabled && !connected) {
        if (re_connect) {
            logger->error (__FILE__, __LINE__, "Try to connect to database (re-connect)");
        }
        if (db->connect (db_data)) {
            double duration = difftime (time (NULL), start_time);

            int day = duration / 86400;
            int seconds = (long) duration % 86400L;
            int hour = seconds / 3600;
            int min = (seconds % 3600) / 60;
            int sec = seconds % 60;

            connected = true;
            gettimeofday (&connection_time, NULL);

            if (day > 0) {
                logger->notice (__FILE__, __LINE__, "Uptime: %d day(s), %02d:%02d:%02d", day, hour, min, sec);
            } else {
                logger->notice (__FILE__, __LINE__, "Uptime: %02d:%02d:%02d", hour, min, sec);
            }
        } else {
            logger->error (__FILE__, __LINE__, "Failed to connect to database");
        }
    }
    gettimeofday (&recent_use_time, NULL);

    pthread_mutex_unlock (&connection_mutex);
    return connected;
}

static bool reconnect_to_database() {
    logger->warning (__FILE__, __LINE__, "Reconnect to database (disconnect and connect again)");

    pthread_mutex_lock (&connection_mutex);
    db->disconnect (db_data);
    connected = false;
    pthread_mutex_unlock (&connection_mutex);
    return connect_to_database (true);
}

static void safe_reconnect_to_database (va_list ap) {
    int counter = 0;

    while (!reconnect_to_database()) {
        sleep (10);
        if (++counter > 20) return;
    }
}

static void try_reconnect (const char * file, const int line) {
    if (try_catch (safe_reconnect_to_database)) {
        logger->error (__FILE__, __LINE__, "Got exception (%s:%d)", file, line);
        exit (139);
    }
}

static struct db_xsql_stmt_t *create_statement (const char *file, const int line, const char *query) {
    int tried = 0;
    bool have_error = false;

    do {
        if (have_error) {
            logger->error (__FILE__, __LINE__, "create statement: %s", query);
        }
        struct db_xsql_stmt_t *stmt = db->createStatement (db_data, query);

        if (stmt != NULL) {
            return stmt;
        } else {
            have_error = true;

            logger->error (__FILE__, __LINE__, "%s: %d, Query: %s, error (%d): %s",
                           file, line, query,
                           db->errno (db_data),
                           db->error (db_data));

            if (tried++ > 0) {
                sleep (10);
            }

            try_reconnect (file, line);
        }
    } while (tried < 60);

    logger->error (__FILE__, __LINE__, "Failed to reconnect to database (give up)");

    return NULL;
}

static struct queries_and_statements *retrieve_statement (const char *file, const int line, enum query_name_enum queryNameEnum) {
    int i;

    for (i = 0; i < sizeof (stmt_holder) / sizeof (struct queries_and_statements); i++) {
        if (stmt_holder[i].index == queryNameEnum) {
            if (stmt_holder[i].stmt == NULL && stmt_holder[i].query != NULL) {
                logger->notice (__FILE__, __LINE__, "create statement: [ %s ]", stmt_holder[i].query);
                stmt_holder[i].stmt = create_statement (file, line, stmt_holder[i].query);
            }

            if (stmt_holder[i].stmt != NULL) {
//                stmt_holder[i].stmt->reset (stmt_holder[i].stmt);
                stmt_holder[i].stmt->clearParameters (stmt_holder[i].stmt);
            }
            return &stmt_holder[i];
        }
    }

    return &stmt_not_found;
}

static struct db_proxy_request_t *check_available (const char *remote_ip) {
    struct db_proxy_request_t *request = NULL;

    if (connect_to_database (false)) {
        struct queries_and_statements *ptr = retrieve_statement (__FILE__, __LINE__, SQL_CHECK_AVAILABLE);

        if (ptr->stmt != NULL) {
            struct db_xsql_stmt_t *stmt = ptr->stmt;
            int errcode = 0;

            stmt->setString (stmt, 1, remote_ip);
            struct db_xsql_result_t *result = stmt->executeQuery (stmt, &errcode);

            if (result != NULL) {
                unsigned int errno = 0;

                if (result->next (result, &errno)) {
                    request = malloc (sizeof (struct db_proxy_request_t *));

                    request->sn = result->getInt (result, 1);
                    request->account = result->getString (result, 2);
                    if (request->account != NULL) {
                        request->account = strdup (request->account);
                    }
                    request->channel = result->getInt (result, 3);
                }
                result->close (result);

                if (errno != 0) {
                    try_reconnect (__FILE__, __LINE__);
                }
            } else {
//                result->close (result);
                logger->error (__FILE__, __LINE__, "failed to executeQuery (%d): %s [%s]", errcode, ptr->query, remote_ip);
                try_reconnect (__FILE__, __LINE__);
            }
        }
    }
    return request;
}

static void connection_close (const int sn, const ssize_t bytes, const int count, const bool idle) {
    if (connect_to_database (false)) {
        struct queries_and_statements *ptr = retrieve_statement (__FILE__, __LINE__, SQL_CONNECTION_CLOSE);

        if (ptr->stmt != NULL) {
            struct db_xsql_stmt_t *stmt = ptr->stmt;

            stmt->setInt (stmt, 1, bytes);
            stmt->setInt (stmt, 2, count);
            stmt->setString (stmt, 3, idle ? "timeout" : "normal");
            stmt->setInt (stmt, 4, sn);
            stmt->executeUpdate (stmt, NULL);
        } else if (ptr->query != NULL) {
            logger->error (__FILE__, __LINE__, "failed to create statement (update-connection): %s", ptr->query);
        }
    }
}

static int connection_established (const int sn, const char *account, const char *ipaddr) {
    int last_insert_id = 0;

    if (connect_to_database (false)) {
        struct queries_and_statements *ptr = retrieve_statement (__FILE__, __LINE__, SQL_CONNECTION_ESTABLISHED);

        if (ptr->stmt != NULL) {
            struct db_xsql_stmt_t *stmt = ptr->stmt;

            stmt->setInt (stmt, 1, sn);
            stmt->executeUpdate (stmt, NULL);

            struct queries_and_statements *ptr2 = retrieve_statement (__FILE__, __LINE__, SQL_CONNECTION_BEGIN);

            if (ptr2->stmt != NULL) {
                struct db_xsql_stmt_t *stmt2 = ptr2->stmt;

                stmt2->setString (stmt2, 1, ipaddr != NULL ? ipaddr : "");
                stmt2->setString (stmt2, 2, account != NULL ? account : "");
                stmt2->executeUpdate (stmt2, NULL);

                struct queries_and_statements *ptr3 = retrieve_statement (__FILE__, __LINE__, SQL_LAST_INSERT_ID);

                if (ptr3->stmt != NULL) {
                    struct db_xsql_stmt_t *stmt3 = ptr3->stmt;

                    struct db_xsql_result_t *result = stmt3->executeQuery (stmt3, NULL);
                    if (result != NULL) {
                        if (result->next (result, NULL)) {
                            last_insert_id = result->getInt (result, 1);
                        }
                        result->close (result);
                    }
                    // stmt3->reset (stmt3);
                } else {
                    logger->error (__FILE__, __LINE__, "failed to create statement (last-insert-id): %s", ptr3->query);
                }
            } else {
                logger->error (__FILE__, __LINE__, "failed to create statement (begin): %s", ptr2->query);
            }
        } else {
            logger->error (__FILE__, __LINE__, "failed to create statement (established): %s", ptr->query);
        }
    }
    return last_insert_id;
}

static void connection_not_allowed (const char *ipaddr) {
    if (connect_to_database (false)) {
        struct queries_and_statements *ptr = retrieve_statement (__FILE__, __LINE__, SQL_CONNECTION_NOT_ALLOWED);

        if (ptr->stmt != NULL) {
            struct db_xsql_stmt_t *stmt = ptr->stmt;

            stmt->setString (stmt, 1, ipaddr);
            stmt->executeUpdate (stmt, NULL);

        } else if (ptr->query != NULL) {
            logger->error (__FILE__, __LINE__, "failed to create statement (not-allowed): %s", ptr->query);
        }
    }
}

static bool read_all_product_names (void (*callback) (char *, char *, char *)) {
    if (connect_to_database (false)) {
        struct queries_and_statements *ptr = retrieve_statement (__FILE__, __LINE__, SQL_ALL_PRODUCT_NAMES);

        if (ptr != NULL && ptr->stmt != NULL) {
            struct db_xsql_stmt_t *stmt = ptr->stmt;
            struct db_xsql_result_t *result = stmt->executeQuery (stmt, NULL);

            if (result != NULL) {
                while (result->next (result, NULL)) {
                    char *app_id = result->getString (result, 1);
                    char *kms_id = result->getString (result, 2);
                    char *product = result->getString (result, 3);

                    callback (app_id, kms_id, product);
                }
                result->close (result);
            }
        }
    }
    return true;
}

static int check_vip (const char *ipaddr) {
    if (connect_to_database (false)) {
        struct queries_and_statements *ptr = retrieve_statement (__FILE__, __LINE__, SQL_CHECK_VIP);

        if (ptr->stmt != NULL) {
            struct db_xsql_stmt_t *stmt = ptr->stmt;

            stmt->setString (stmt, 1, ipaddr);
            int affected_rows = stmt->executeUpdate (stmt, NULL);

            return affected_rows;
        } else if (ptr->query != NULL) {
            logger->error (__FILE__, __LINE__, "failed to create statement (vip-checking): %s", ptr->query);
        }
    }
    return 0;
}

static int connection_blacklisted (const char *ipaddr) {
    if (connect_to_database (false)) {
        struct queries_and_statements *ptr = retrieve_statement (__FILE__, __LINE__, SQL_BLACKLIST);
        struct db_xsql_stmt_t *stmt = ptr->stmt;

        if (stmt != NULL) {
            stmt->setString (stmt, 1, ipaddr);
            int affected_rows = stmt->executeUpdate (stmt, NULL);

            return affected_rows;
        } else if (ptr->query != NULL) {
            logger->error (__FILE__, __LINE__, "failed to create statement (update-bl-count): %s", ptr->query);
        }
    }
    return 0;
}

static int add_ip_to_auto_blacklist (const char *ipaddr) {
    if (connect_to_database (false)) {

        struct queries_and_statements *ptr = retrieve_statement (__FILE__, __LINE__, SQL_ADD_TO_BLACKLIST);
        struct db_xsql_stmt_t *stmt = ptr->stmt;

        if (stmt != NULL) {
            stmt->setString (stmt, 1, ipaddr);
            int affected_rows = stmt->executeUpdate (stmt, NULL);
//                stmt->close (stmt);

            return affected_rows;
        } else if (ptr->query != NULL) {
            logger->error (__FILE__, __LINE__, "failed to create statement (add-to-blacklist): %s", ptr->query);
        }
    }
    return 0;
}

static int add_kms_details (const struct connection_info *info,
                            const char * const workstation,
                            const int major_version,
                            const int minor_version,
                            const char * const app_id,
                            const char * const kms_id,
                            const char * const client_machine_id,
                            const int remaining_min) {

    if (connect_to_database (false)) {
        struct queries_and_statements *ptr = retrieve_statement (__FILE__, __LINE__, SQL_ADD_DETAILS);
        struct db_xsql_stmt_t *stmt = ptr->stmt;

        // (account,ipaddr,workstation,major_version,minor_version,app_id,kms_id,cmid,remaining_min,created_at) VALUES (?,?,?,?,?,?,?,?,?,NOW())";

        if (stmt != NULL) {
            stmt->clearParameters (stmt);
            stmt->setString (stmt, 1, info->request_in_db != NULL ? info->request_in_db->account : "");
            stmt->setString (stmt, 2, info->remote_ip);
            stmt->setString (stmt, 3, workstation);
            stmt->setInt (stmt, 4, major_version);
            stmt->setInt (stmt, 5, minor_version);
            stmt->setString (stmt, 6, app_id);
            stmt->setString (stmt, 7, kms_id);
            stmt->setString (stmt, 8, client_machine_id);
            stmt->setInt (stmt, 9, remaining_min);
            return stmt->executeUpdate (stmt, NULL);
        } else if (ptr->query != NULL) {
            logger->error (__FILE__, __LINE__, "failed to create statement (add-details): %s", ptr->query);
        }
    }
    return 0;
}

static int update_machine_owner (const struct connection_info *info, const char * const client_machine_id) {
    if (connect_to_database (false)) {
        const char *const account = info->request_in_db != NULL ? info->request_in_db->account : NULL;

        struct queries_and_statements *ptr = retrieve_statement (__FILE__, __LINE__,
                                             account != NULL ? SQL_ADD_MACHINE_OWNER : SQL_UPDATE_MACHINE_ACCESS);
        struct db_xsql_stmt_t *stmt = ptr->stmt;

        if (stmt != NULL) {
            // (cmid,account,recent_ip,counter,created_at,updated_at) VALUES (?,?,?,1,NOW(),NOW()) ON DUPLICATE KEY UPDATE recent_ip=?,counter=counter+1,account=?";
            stmt->clearParameters (stmt);
            stmt->setString (stmt, 1, client_machine_id);
            stmt->setString (stmt, 2, account != NULL ? account : "");
            stmt->setString (stmt, 3, info->remote_ip);
            stmt->setString (stmt, 4, info->remote_ip);
            if (account != NULL) {
                stmt->setString (stmt, 5, account);
            }
            return stmt->executeUpdate (stmt, NULL);
        }
    }
    return 0;
}

struct fail_guessing_pad_load_t {
    const char * const ip_address;
    bool result;
};

static void fail_guessing_handler (struct db_xsql_result_t *result, void *data) {
    struct fail_guessing_pad_load_t *padLoad = (struct fail_guessing_pad_load_t *) data;
    int count = 0;

    if (result->next (result, NULL)) {
        count = result->getInt (result, 1);
    }

    padLoad->result = count > 5;
}

static bool fail_guessing (const char * const ip_address) {
    if (connect_to_database (false)) {
        struct queries_and_statements *ptr = retrieve_statement (__FILE__, __LINE__, SQL_CALL_FAILURE_GUESSING);

        if (ptr != NULL && ptr->stmt != NULL) {
            struct db_xsql_stmt_t *stmt = ptr->stmt;

            stmt->clearParameters (stmt);
            stmt->setString (stmt, 1, ip_address);

            struct fail_guessing_pad_load_t padLoad = {
                .ip_address = ip_address,
                .result = false,
            };

            stmt->executeMultipleQuery (stmt, NULL, &padLoad, fail_guessing_handler);

            return padLoad.result;
        }
    }
    return false;
}


static void close_all_statements() {
    int i;

    for (i = 0; i < sizeof (stmt_holder) / sizeof (struct queries_and_statements); i++) {
        if (stmt_holder[i].stmt != NULL) {
            logger->notice (__FILE__, __LINE__, "close statement [ %s ]", stmt_holder[i].query);
            stmt_holder[i].stmt->close (stmt_holder[i].stmt);
            stmt_holder[i].stmt = NULL;
        }
    }
}

static void close_idle (const struct timeval *tv, const struct tm *tm) {
    pthread_mutex_lock (&connection_mutex);

    if (connected) {
        double elapsed = elapsed_time (tv, &recent_use_time);

        if (elapsed > database_timeout) {
            logger->error (__FILE__, __LINE__, "IDLE ... close database connection");

            close_all_statements();

            db->disconnect (db_data);
            connected = false;
        }
    }

    pthread_mutex_unlock (&connection_mutex);
}

static void db_service_done() {
    static bool inited = false;
    static double max_connection_time = 1200.;

    if (!inited) {
        max_connection_time = (double) system_conf->int_or_default ("max-db-connection-time", 3600);
        inited = true;
    }

    pthread_mutex_lock (&connection_mutex);

    if (connected) {
        struct timeval tv;

        gettimeofday (&tv, NULL);
        double elapsed = elapsed_time (&tv, &connection_time);

        if (elapsed > max_connection_time) {
            logger->notice (__FILE__, __LINE__, "Database connection time reaching maximum: %.2f seconds, [ close ]",
                            elapsed);
            close_all_statements();
            db->disconnect (db_data);
            connected = false;
        }
    }

    pthread_mutex_unlock (&connection_mutex);
}

static void set_logger (struct logger_t *new_logger) {
    if (db != NULL) {
        db->setLogger (new_logger);
    }

    logger = new_logger;
}


static struct hash_map_t *product_name_hash;

static const char * allocate_product_name_key (const char *const app_id, const char *const kms_id) {
    char *key = malloc (strlen (app_id) + strlen (kms_id) + 2);
    sprintf (key, "%s-%s", app_id, kms_id);
    return key;
}

static void product_names_callback (char *app_id, char *kms_id, char *product_name) {
    const char * key = allocate_product_name_key (app_id, kms_id);
    const void *origin = product_name_hash->put (product_name_hash, key, strdup (product_name));

    if (origin != NULL) {
        free ((void *) key);
        free ((void *) origin);
    }
}

static void reload_product_names (void) {
    read_all_product_names (product_names_callback);
}

static const char *const context_name (void) {
    const static char *const name = DATABASE_SERVICE_DEFAULT_CONTEXT_NAME;
    return name;
}

static const char * const get_product_name (const char *const app_id, const char * const kms_id) {
    const char *key = allocate_product_name_key (app_id, kms_id);

    const void *value = product_name_hash->get (product_name_hash, key);
    free ((void*) key);
    return value;
}

static void post_construct (void) {
    logger->trace (__FILE__, __LINE__, "%s:%d %s", __FILE__, __LINE__, __FUNCTION__ );
}

static struct database_service_t instance = {
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
    .check_available = check_available,
    .connection_close = connection_close,
    .connection_established = connection_established,
    .connection_not_allowed = connection_not_allowed,
    .connection_blacklisted = connection_blacklisted,
    .add_ip_to_auto_blacklist = add_ip_to_auto_blacklist,
    .add_kms_details = add_kms_details,
    .update_machine_owner = update_machine_owner,
    .reload_product_names = reload_product_names,
    .get_product_name = get_product_name,
    .fail_guessing = fail_guessing,
    .check_vip = check_vip,
    .close_idle = close_idle,
    .done = db_service_done,
    .set_logger = set_logger,
};

struct database_service_t *new_database_service (struct system_config_t *sysconf) {
    if (system_conf == NULL) {
        system_conf = sysconf;

        logger = get_application_context()->get_logger ();

        enabled = sysconf->int_or_default ("enable-database", 0) != 0;

        product_name_hash = new_hash_map (53, NULL);

        start_time = time (NULL);

        if (enabled) {
            int i;

            db = init_db_mysql();
            db_data = db->newInstance();

            db_connection_info.dbhost = sysconf->str ("mysql-server");
            db_connection_info.dbuser = sysconf->str ("mysql-account");
            db_connection_info.dbpasswd = sysconf->str ("mysql-passwd");
            db_connection_info.dbname = sysconf->str ("mysql-database");
            db->setInfo (db_data, &db_connection_info);

            for (i = 0; i < sizeof (stmt_holder) / sizeof (struct queries_and_statements); i++) {
                if (stmt_holder[i].query_name != NULL) {
                    if (stmt_holder[i].query == NULL) {
                        stmt_holder[i].query = sysconf->str (stmt_holder[i].query_name);
                    }

                    if (stmt_holder[i].query != NULL) {
                        logger->debug (__FILE__, __LINE__, "%d: (%d) %s [%s]", i, stmt_holder[i].index, stmt_holder[i].query_name, stmt_holder[i].query);
                    }
                }
            }
        }

        connected = false;
    }

    return &instance;
}
