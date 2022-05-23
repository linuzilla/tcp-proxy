#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
//#include <my_global.h>
//#include <my_sys.h>

#if MYSQL_VERSION == 10
#include <mysql.h>
#else

#include <mysql/mysql.h>

#  if MYSQL_VERSION >= 8
typedef bool my_bool;
#  endif
#endif

#include <assert.h>
#include "db_xsql.h"
#include "db_conninfo.h"
#include "logger.h"

// #define MAX_STRING_SIZE	4096

//#if MYSQL_VERSION >= 8
//typedef bool my_bool;
//#endif

static int instance_id = 0;

struct db_mysql_data_t {
    struct db_connection_info_t *info;
    MYSQL mysql;
    bool haveInit;
    bool connected;
    int instance_id;
};

struct db_mysql_result_bind_data_t {
    unsigned long length;
    my_bool is_null;
    my_bool error;
    union {
        short short_data;
        unsigned int uint_data;
        int int_data;
        int64_t int64_data;
        uint64_t uint64_data;
        MYSQL_TIME ts;
    };
    unsigned long real_length;
    char *str_data;
};

struct db_mysql_stmt_t {
    struct db_mysql_data_t *data;
    MYSQL_STMT *statement;
    MYSQL_BIND *param_bind;
    struct db_mysql_result_bind_data_t *param_bind_data;
    MYSQL_BIND *result_bind;
    struct db_mysql_result_bind_data_t *result_bind_data;
    int param_count;
    unsigned int number_fields;
    unsigned long affected_rows;
};

struct db_mysql_result_t {
    struct db_mysql_stmt_t *stmt;
    my_ulonglong number_rows;
    unsigned int number_fields;
    struct db_xsql_field_t *fields;
};

static struct logger_t *logger = &excalibur_common_logger;

static void dispose (struct db_xsql_data_t *self) {
    if (self != NULL) {
        if (self->data != NULL) {
            free (self->data);
        }
        free (self);
    }
}

static struct db_xsql_data_t *newInstance (void) {
    struct db_xsql_data_t *ptr = malloc (sizeof (struct db_xsql_data_t));

    if (ptr != NULL) {
        struct db_mysql_data_t *self;

        if ((self = ptr->data = malloc (
                                    sizeof (struct db_mysql_data_t))) != NULL) {
            self->connected = false;
            self->haveInit = false;
            self->instance_id = ++instance_id;

            if (mysql_init (&self->mysql) == NULL) {
                logger->error (__FILE__, __LINE__, "Failed to mysql_init");
            } else {
                self->haveInit = true;

                logger->info (__FILE__, __LINE__,
                              "MySQL Client Version is %s",
                              mysql_get_client_info()
                             );
            }
        } else {
            dispose (ptr);
            ptr = NULL;
        }
    }
    return ptr;
}

static bool dbmysql_connect (struct db_xsql_data_t *data) {
    struct db_mysql_data_t *self = (struct db_mysql_data_t *) data->data;

    if (self->connected) return true;

    mysql_init (&self->mysql);

    if (!mysql_real_connect (&self->mysql,
                             self->info->dbhost,
                             self->info->dbuser,
                             self->info->dbpasswd,
                             self->info->dbname, 0, NULL, CLIENT_MULTI_STATEMENTS)) {
        logger->error (__FILE__, __LINE__,
                       "Failed to connect to MySQL [%d]: Error: %s",
                       self->instance_id, mysql_error (&self->mysql));
        return false;
    } else {
        self->connected = true;

        logger->notice (__FILE__, __LINE__,
                        "MySQL Server Version is %s [%d] (host=%s,user=%s,pass=***,db=%s)",
                        mysql_get_server_info (&self->mysql),
                        self->instance_id,
                        self->info->dbhost,
                        self->info->dbuser,
                        self->info->dbname
                       );

        if (mysql_select_db (&self->mysql, self->info->dbname) != 0) {
            logger->error (__FILE__, __LINE__, mysql_error (&self->mysql));
        }

        mysql_set_character_set (&self->mysql, "utf8");
        return true;
    }
}

static void dbmysql_disconnect (struct db_xsql_data_t *data) {
    struct db_mysql_data_t *self = (struct db_mysql_data_t *) data->data;

    if (self->connected) {
        logger->notice (__FILE__, __LINE__, "Disconnect [%d]: %s",
                        self->instance_id,
                        mysql_stat (&self->mysql)
                       );
        mysql_close (&self->mysql);
        self->connected = false;
    }
}

static void dbmysql_setInfo (struct db_xsql_data_t *data,
                             struct db_connection_info_t *info) {
    struct db_mysql_data_t *self = (struct db_mysql_data_t *) data->data;
    self->info = info;
}

static void dbmysql_result_close (struct db_xsql_result_t *result) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;

    if (mysql_stmt_free_result (res->stmt->statement)) {
        int error_no = mysql_stmt_errno (res->stmt->statement);

        logger->warning (__FILE__, __LINE__, "Failed to free result (%d)", error_no);
    }

    if (res->number_fields > 0 && res->fields != NULL) {
        int i;
        for (i = 0; i < res->number_fields; i++) {
            free (res->fields[i].fieldName);
        }
        free (res->fields);
    }
    free (res);
    free (result);
}

static time_t dbmysql_result_getTimestamp (struct db_xsql_result_t *result, const int parameterIndex) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;
    struct db_mysql_stmt_t *stmt = res->stmt;

    if (parameterIndex >= 1 && parameterIndex <= res->number_fields) {
        int i = parameterIndex - 1;
        MYSQL_TIME *ts = &stmt->result_bind_data[i].ts;
        struct tm tm;
        time_t ret;

        tm.tm_year = ts->year - 1900;
        tm.tm_mon = ts->month - 1;
        tm.tm_mday = ts->day;
        tm.tm_hour = ts->hour;
        tm.tm_min = ts->minute;
        tm.tm_sec = ts->second;
        tm.tm_isdst = -1;

        if ((ret = mktime (&tm)) == -1) {
            return 0;
        }
        return ret;
    }
    return 0;
}

static int dbmysql_result_getInt (struct db_xsql_result_t *result, const int parameterIndex) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;
    struct db_mysql_stmt_t *stmt = res->stmt;

    if (parameterIndex >= 1 && parameterIndex <= res->number_fields) {
        int i = parameterIndex - 1;

        if (stmt->result_bind_data[i].is_null) {
            logger->error (__FILE__, __LINE__, "getInt(%d / %d) null value", parameterIndex, res->number_fields);
            return 0;
        } else {
//            logger->error(__FILE__, __LINE__, "getInt(%d / %d) value = %d, %ld", parameterIndex, res->number_fields,
//                          stmt->result_bind_data[i].int_data,
//                          stmt->result_bind_data[i].int64_data);

            return stmt->result_bind_data[i].int_data;
        }
    } else {
        logger->error (__FILE__, __LINE__, "out of range: %d vs %d", parameterIndex, res->number_fields);
    }

    return 0;
}

static short dbmysql_result_getShort (struct db_xsql_result_t *result, const int parameterIndex) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;
    struct db_mysql_stmt_t *stmt = res->stmt;

    if (parameterIndex >= 1 && parameterIndex <= res->number_fields) {
        int i = parameterIndex - 1;

        if (stmt->result_bind_data[i].is_null) {
            return 0;
        } else {
            return stmt->result_bind_data[i].short_data;
        }
    } else {
        logger->error (__FILE__, __LINE__, "out of range: %d vs %d", parameterIndex, res->number_fields);
    }

    return 0;
}

static unsigned int dbmysql_result_getuInt (struct db_xsql_result_t *result, const int parameterIndex) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;
    struct db_mysql_stmt_t *stmt = res->stmt;

    if (parameterIndex >= 1 && parameterIndex <= res->number_fields) {
        int i = parameterIndex - 1;

        if (stmt->result_bind_data[i].is_null) {
            return 0;
        } else {
            return stmt->result_bind_data[i].uint_data;
        }
    } else {
        logger->error (__FILE__, __LINE__, "out of range: %d vs %d", parameterIndex, res->number_fields);
    }

    return 0;
}

static uint64_t dbmysql_result_getuBigint (struct db_xsql_result_t *result, const int parameterIndex) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;
    struct db_mysql_stmt_t *stmt = res->stmt;

    if (parameterIndex >= 1 && parameterIndex <= res->number_fields) {
        int i = parameterIndex - 1;

        if (stmt->result_bind_data[i].is_null) {
            return 0;
        } else {
            return stmt->result_bind_data[i].uint64_data;
        }
    } else {
        logger->error (__FILE__, __LINE__, "out of range: %d vs %d", parameterIndex, res->number_fields);
    }

    return 0;
}

static int64_t dbmysql_result_getBigint (struct db_xsql_result_t *result, const int parameterIndex) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;
    struct db_mysql_stmt_t *stmt = res->stmt;

    if (parameterIndex >= 1 && parameterIndex <= res->number_fields) {
        int i = parameterIndex - 1;

        if (stmt->result_bind_data[i].is_null) {
            return 0;
        } else {
            return stmt->result_bind_data[i].int64_data;
        }
    } else {
        logger->error (__FILE__, __LINE__, "out of range: %d vs %d", parameterIndex, res->number_fields);
    }

    return 0;
}

static char *dbmysql_result_getString (struct db_xsql_result_t *result, const int parameterIndex) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;
    struct db_mysql_stmt_t *stmt = res->stmt;

    if (parameterIndex >= 1 && parameterIndex <= res->number_fields) {
        int i = parameterIndex - 1;

        if (stmt->result_bind_data[i].is_null) {
            logger->debug (__FILE__, __LINE__, "data: NULL");
            return NULL;
        } else if (stmt->result_bind_data[i].real_length > 0) {
            MYSQL_BIND bind;
            my_bool is_null;
            my_bool is_error;
            unsigned long length;

            // fprintf (stderr, "%s(%d) real_length=%d\n",
            //		__FILE__, __LINE__, stmt->result_bind_data[i].real_length);
            //

            if (stmt->result_bind_data[i].str_data != NULL) {
                free (stmt->result_bind_data[i].str_data);
                stmt->result_bind_data[i].str_data = NULL;
            }

            unsigned long data_size = stmt->result_bind_data[i].real_length + 1;
            char *data = malloc (data_size);

            bind.buffer_type = MYSQL_TYPE_STRING;
            bind.is_null = &is_null;
            bind.error = &is_error;
            bind.length = &length;
            bind.is_unsigned = 0;
            bind.buffer = data;
            bind.buffer_length = stmt->result_bind_data[i].real_length;

            if (mysql_stmt_fetch_column (stmt->statement, &bind, i, 0) == 0) {
                if (is_null) {
                    free (data);
                    logger->warning (__FILE__, __LINE__, "getString(%d): data is NULL unexpectedly", i);
                    return NULL;
                } else if (is_error) {
                    free (data);
                    logger->warning (__FILE__, __LINE__, "getString(%d): has error", i);
                    return NULL;
                }
                data[length] = '\0';
                stmt->result_bind_data[i].str_data = data;

                if (logger->isEnable (log_trace)) {
                    logger->trace (__FILE__, __LINE__, "getString(%d): data: [%s]", i, data);
                }
                return data;
            } else {
                logger->error (__FILE__, __LINE__, "getString(%d): %s", i, mysql_stmt_error (stmt->statement));
                free (data);
                return NULL;
            }
        } else {
            logger->error (__FILE__, __LINE__, "getString(%d): real_length=%ld",
                           i,
                           stmt->result_bind_data[i].real_length);
        }
    } else {
        logger->error (__FILE__, __LINE__, "data: out of range (%d vs %d)", parameterIndex, res->number_fields);

    }

    return NULL;
}

static struct db_xsql_field_t *dbmysql_result_getFields (struct db_xsql_result_t *result) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;
    return res->fields;
}

static int dbmysql_result_getIndex (struct db_xsql_result_t *result, const char *str) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;
    int i;

    for (i = 0; i < res->number_fields; i++) {
        if (strcmp (res->fields[i].fieldName, str) == 0) {
            return i + 1;
        }
    }
    return 0;
}


static unsigned int dbmysql_result_getNumberOfFields (struct db_xsql_result_t *result) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;
    return res->number_fields;
}

static bool dbmysql_result_next (struct db_xsql_result_t *result, unsigned int *errno) {
    struct db_mysql_result_t *res = (struct db_mysql_result_t *) result->data;
    struct db_mysql_stmt_t *stmt = res->stmt;

    if (errno != NULL) {
        *errno = 0;
    }

    switch (mysql_stmt_fetch (stmt->statement)) {
    case 0: // successful
    case MYSQL_DATA_TRUNCATED: // data truncation occurred
        return true;

    case 1: // error
        if (errno != NULL) {
            *errno = mysql_stmt_errno (stmt->statement);
        }
        logger->error (__FILE__, __LINE__, "next(mysql_stmt_fetch): %s",
                       mysql_stmt_error (stmt->statement));
        return false;
    case MYSQL_NO_DATA:    // no more rows/data exists
        // fprintf (stderr, "\n%s(%d): no more data\n", __FILE__, __LINE__);
        return false;
    }

    logger->error (__FILE__, __LINE__, "oops!");

    return false;
}

static bool dbmysql_local_execute (struct db_xsql_stmt_t *self, int *errcode) {
    struct db_mysql_stmt_t *stmt = self->data;

    if (errcode != NULL) {
        *errcode = 0;
    }

    if (stmt->param_count > 0) {
        if (mysql_stmt_bind_param (
                    stmt->statement, stmt->param_bind) != 0) {
            logger->error (__FILE__, __LINE__, "bind: %s",
                           mysql_stmt_error (stmt->statement));
            return false;
        }
    }

    if (mysql_stmt_execute (stmt->statement)) {
        if (errcode != NULL) {
            *errcode = mysql_stmt_errno (stmt->statement);
        }
        logger->error (__FILE__, __LINE__, "execute (%d): %s", mysql_stmt_errno (stmt->statement), mysql_stmt_error (stmt->statement));
        return false;
    }

    return true;
}

/*
static void dbmysql_stmt_free_result (struct db_xsql_stmt_t *self) {
    struct db_mysql_stmt_t *stmt = (struct db_mysql_stmt_t *) self->data;

    if (stmt->statement != NULL) {
        mysql_stmt_free_result (stmt->statement);
    }
}
*/

static void dbmysql_clearParameters (struct db_xsql_stmt_t *self) {
    struct db_mysql_stmt_t *stmt = (struct db_mysql_stmt_t *) self->data;
    int i;

    if (stmt->param_count > 0) {
        for (i = 0; i < stmt->param_count; i++) {
            memset (& (stmt->param_bind[i]), 0, sizeof (MYSQL_BIND));

            stmt->param_bind[i].buffer_type = MYSQL_TYPE_NULL;
            stmt->param_bind[i].is_null = & (stmt->param_bind_data[i].is_null);
            stmt->param_bind[i].error = & (stmt->param_bind_data[i].error);
            stmt->param_bind[i].length = & (stmt->param_bind_data[i].length);
            stmt->param_bind[i].is_unsigned = 0;

            stmt->param_bind_data[i].is_null = 1;
            stmt->param_bind_data[i].error = 0;
            stmt->param_bind_data[i].length = 0;

            if (stmt->param_bind_data[i].str_data != NULL) {
                free (stmt->param_bind_data[i].str_data);
                stmt->param_bind_data[i].str_data = NULL;
            }
        }
    }
}

static int dbmysql_executeUpdate (struct db_xsql_stmt_t *self, int *errcode) {
    struct db_mysql_stmt_t *stmt = self->data;

    if (!dbmysql_local_execute (self, errcode)) return -1;

    stmt->affected_rows = mysql_stmt_affected_rows (stmt->statement);
    mysql_stmt_free_result (stmt->statement);

    // fprintf (stderr, "%s(%d): total affected rows: %lu\n", __FILE__, __LINE__, stmt->affected_rows);

    return stmt->affected_rows;
}

static int error_number (struct db_xsql_data_t *data) {
    struct db_mysql_data_t *self = (struct db_mysql_data_t *) data->data;

    return self->connected ? mysql_errno (&self->mysql) : 0;
}

static const char *error_string (struct db_xsql_data_t *data) {
    struct db_mysql_data_t *self = (struct db_mysql_data_t *) data->data;

    return self->connected ? mysql_error (&self->mysql) : "";
}

static struct db_xsql_result_t * internal_fetch_result (struct db_mysql_stmt_t *stmt) {
    struct db_xsql_result_t *res = malloc (sizeof (struct db_xsql_result_t));
    struct db_mysql_result_t *result = malloc (sizeof (struct db_mysql_result_t));

    res->data = result;
    result->stmt = stmt;

    res->close = dbmysql_result_close;
    res->next = dbmysql_result_next;

    res->getInt = dbmysql_result_getInt;
    res->getShort = dbmysql_result_getShort;
    res->getuInt = dbmysql_result_getuInt;
    res->getBigint = dbmysql_result_getBigint;
    res->getuBigint = dbmysql_result_getuBigint;
    res->getString = dbmysql_result_getString;
    res->getTimestamp = dbmysql_result_getTimestamp;

    res->getIndex = dbmysql_result_getIndex;

    res->getNumberOfFields = dbmysql_result_getNumberOfFields;
    res->getFields = dbmysql_result_getFields;

    result->number_rows = mysql_stmt_num_rows (stmt->statement);

    //fprintf (stderr, "%s(%d): total number of rows: %llu\n", __FILE__, __LINE__,
    //	result->number_rows);

    MYSQL_RES *result_meta = mysql_stmt_result_metadata (stmt->statement);

    if (result_meta == NULL) {
        int errorNo = mysql_errno (&stmt->data->mysql);
        logger->error (__FILE__, __LINE__, "No meta information exists (errno %d): %s", errorNo,
                       mysql_stmt_error (stmt->statement));

        free (res);
        free (result);
        return NULL;
    }

    MYSQL_FIELD *field;

    result->number_fields = mysql_num_fields (result_meta);
    result->stmt->number_fields = result->number_fields;

    MYSQL_BIND *bind = calloc (
                           result->number_fields, sizeof (MYSQL_BIND)
                       );

    result->fields = calloc (result->number_fields,
                             sizeof (struct db_xsql_field_t));

    struct db_mysql_result_bind_data_t *data = calloc (
                result->number_fields,
                sizeof (struct db_mysql_result_bind_data_t)
            );

    stmt->result_bind = bind;
    stmt->result_bind_data = data;

    int i;

    for (i = 0; ((field = mysql_fetch_field (result_meta))); i++) {
        result->fields[i].fieldName = strdup (field->name);
        result->fields[i].fieldType = field->type;

        bind[i].is_null = &data[i].is_null;
        bind[i].length = &data[i].length;
        bind[i].error = &data[i].error;
        bind[i].is_unsigned = 0;
        bind[i].buffer = NULL;
        bind[i].buffer_length = 0;
        bind[i].length = & (data[i].real_length);

        data[i].str_data = NULL;
        data[i].real_length = 0L;
        // printf("[%s:%d]", field->name, field->type);

        switch (field->type) {
        case MYSQL_TYPE_NEWDECIMAL:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_INT24:
            bind[i].buffer_type = MYSQL_TYPE_LONG;
            bind[i].buffer = (char *) &data[i].int_data;
            break;

        case MYSQL_TYPE_LONGLONG:
            bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[i].buffer = (char *) &data[i].int64_data;
            break;

        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
            break;

        case MYSQL_TYPE_NULL:
            break;

        case MYSQL_TYPE_TIMESTAMP:
        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_TIME:
        case MYSQL_TYPE_DATETIME:
        case MYSQL_TYPE_YEAR:
        case MYSQL_TYPE_NEWDATE:
            bind[i].buffer_type = MYSQL_TYPE_TIMESTAMP;
            bind[i].buffer = (char *) &data[i].ts;
            break;

        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_ENUM:
            bind[i].buffer_type = MYSQL_TYPE_STRING;
            break;

        case MYSQL_TYPE_SET:

        case MYSQL_TYPE_BIT:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_GEOMETRY:

#if MYSQL_VERSION >= 8
        case MYSQL_TYPE_TIMESTAMP2:
        case MYSQL_TYPE_DATETIME2:
        case MYSQL_TYPE_TIME2:
        case MYSQL_TYPE_JSON:
            //		case MAX_NO_FIELD_TYPES:
#endif
            break;
        }
    }
    mysql_free_result (result_meta);
    mysql_stmt_bind_result (stmt->statement, bind);
//    mysql_stmt_store_result (stmt->statement);

    return res;
}

static bool dbmysql_executeMultipleQuery (struct db_xsql_stmt_t *self, int *errcode, void *padLoad, void (*result_handler) (struct db_xsql_result_t *, void *)) {
    struct db_mysql_stmt_t *stmt = self->data;
    int status;

    if (!dbmysql_local_execute (self, errcode)) {
        return false;
    }

    /* process results until there are no more */
    do {
        int num_fields;       /* number of columns in result */

        /* the column count is > 0 if there is a result set */
        /* 0 if the result is only the final status packet */
        num_fields = mysql_stmt_field_count (stmt->statement);

        if (num_fields > 0) {
            /* there is a result set to fetch */
            logger->trace (__FILE__, __LINE__, "Number of columns in result: %d", (int) num_fields);

            /* what kind of result set is this? */
//            printf ("Data: ");
//            if (stmt->data->mysql.server_status & SERVER_PS_OUT_PARAMS)
//                printf ("this result set contains OUT/INOUT parameters\n");
//            else
//                printf ("this result set is produced by the procedure\n");

            struct db_xsql_result_t *r = internal_fetch_result (stmt);

            if (r != NULL) {
                result_handler (r, padLoad);
                r->close (r);
            }
        } else {
            /* no columns = final status packet */
            logger->trace (__FILE__, __LINE__, "End of procedure output");
        }

        /* more results? -1 = no, >0 = error, 0 = yes (keep looking) */
        status = mysql_stmt_next_result (stmt->statement);
    } while (status == 0);

//    mysql_stmt_close (stmt->statement);
    return true;
}

static struct db_xsql_result_t *dbmysql_executeQuery (struct db_xsql_stmt_t *self, int *errcode) {
    struct db_mysql_stmt_t *stmt = self->data;

    if (!dbmysql_local_execute (self, errcode)) {
        return NULL;
    }

    return internal_fetch_result (stmt);

    /*
    struct db_xsql_result_t *res = malloc (sizeof (struct db_xsql_result_t));
    struct db_mysql_result_t *result = malloc (sizeof (struct db_mysql_result_t));

    res->data = result;
    result->stmt = stmt;

    res->close = dbmysql_result_close;
    res->next = dbmysql_result_next;

    res->getInt = dbmysql_result_getInt;
    res->getShort = dbmysql_result_getShort;
    res->getuInt = dbmysql_result_getuInt;
    res->getBigint = dbmysql_result_getBigint;
    res->getuBigint = dbmysql_result_getuBigint;
    res->getString = dbmysql_result_getString;
    res->getTimestamp = dbmysql_result_getTimestamp;

    res->getIndex = dbmysql_result_getIndex;

    res->getNumberOfFields = dbmysql_result_getNumberOfFields;
    res->getFields = dbmysql_result_getFields;

    result->number_rows = mysql_stmt_num_rows (stmt->statement);

    //fprintf (stderr, "%s(%d): total number of rows: %llu\n", __FILE__, __LINE__,
    //	result->number_rows);

    MYSQL_RES *result_meta = mysql_stmt_result_metadata (stmt->statement);

    if (result_meta == NULL) {
        int errorNo = mysql_errno (&stmt->data->mysql);
        logger->error (__FILE__, __LINE__, "No meta information exists (errno %d): %s", errorNo,
                       mysql_stmt_error (stmt->statement));

        free (res);
        free (result);
        return NULL;
    }

    MYSQL_FIELD *field;

    result->number_fields = mysql_num_fields (result_meta);
    result->stmt->number_fields = result->number_fields;

    MYSQL_BIND *bind = calloc (
                           result->number_fields, sizeof (MYSQL_BIND)
                       );

    result->fields = calloc (result->number_fields,
                             sizeof (struct db_xsql_field_t));

    struct db_mysql_result_bind_data_t *data = calloc (
                result->number_fields,
                sizeof (struct db_mysql_result_bind_data_t)
            );

    stmt->result_bind = bind;
    stmt->result_bind_data = data;

    int i;

    for (i = 0; ((field = mysql_fetch_field (result_meta))); i++) {
        result->fields[i].fieldName = strdup (field->name);
        result->fields[i].fieldType = field->type;

        bind[i].is_null = &data[i].is_null;
        bind[i].length = &data[i].length;
        bind[i].error = &data[i].error;
        bind[i].is_unsigned = 0;
        bind[i].buffer = NULL;
        bind[i].buffer_length = 0;
        bind[i].length = & (data[i].real_length);

        data[i].str_data = NULL;
        data[i].real_length = 0L;
        // printf("[%s:%d]", field->name, field->type);

        switch (field->type) {
        case MYSQL_TYPE_NEWDECIMAL:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_INT24:
            bind[i].buffer_type = MYSQL_TYPE_LONG;
            bind[i].buffer = (char *) &data[i].int_data;
            break;

        case MYSQL_TYPE_LONGLONG:
            bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[i].buffer = (char *) &data[i].int64_data;
            break;

        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
            break;

        case MYSQL_TYPE_NULL:
            break;

        case MYSQL_TYPE_TIMESTAMP:
        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_TIME:
        case MYSQL_TYPE_DATETIME:
        case MYSQL_TYPE_YEAR:
        case MYSQL_TYPE_NEWDATE:
            bind[i].buffer_type = MYSQL_TYPE_TIMESTAMP;
            bind[i].buffer = (char *) &data[i].ts;
            break;

        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_ENUM:
            bind[i].buffer_type = MYSQL_TYPE_STRING;
            break;

        case MYSQL_TYPE_SET:

        case MYSQL_TYPE_BIT:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_GEOMETRY:

    #if MYSQL_VERSION >= 8
        case MYSQL_TYPE_TIMESTAMP2:
        case MYSQL_TYPE_DATETIME2:
        case MYSQL_TYPE_TIME2:
        case MYSQL_TYPE_JSON:
            //		case MAX_NO_FIELD_TYPES:
    #endif
            break;
        }
    }
    mysql_free_result (result_meta);
    mysql_stmt_bind_result (stmt->statement, bind);
    mysql_stmt_store_result (stmt->statement);

    return res;
     */
}

static void dbmysql_stmt_close (struct db_xsql_stmt_t *self) {
    struct db_mysql_stmt_t *stmt = self->data;
    int i;

    mysql_stmt_close (stmt->statement);

    if (stmt->result_bind != NULL) {
        free (stmt->result_bind);
    }
    // fprintf (stderr, "%s(%d): %s\n", __FILE__, __LINE__, __FUNCTION__);
    if (stmt->result_bind_data != NULL) {
        for (i = 0; i < stmt->number_fields; i++) {
            if (stmt->result_bind_data[i].str_data != NULL) {
                free (stmt->result_bind_data[i].str_data);
            }
        }
        free (stmt->result_bind_data);
    }

    if (stmt->param_bind != NULL) {
        free (stmt->param_bind);
    }

    if (stmt->param_bind_data != NULL) {
        if (stmt->param_count > 0) {
            for (i = 0; i < stmt->param_count; i++) {
                if (stmt->param_bind_data[i].str_data != NULL) {
                    free (stmt->param_bind_data[i].str_data);
                }
            }
        }
        free (stmt->param_bind_data);
    }

    free (stmt);
    free (self);
}

static void dbmysql_stmt_setBigint (struct db_xsql_stmt_t *self, const int parameterIndex, const int64_t value) {
    struct db_mysql_stmt_t *stmt = self->data;

    if (parameterIndex >= 1 && parameterIndex <= stmt->param_count) {
        int i = parameterIndex - 1;

        stmt->param_bind_data[i].int64_data = value;
        stmt->param_bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
        stmt->param_bind[i].buffer = (char *) & (stmt->param_bind_data)[i].int64_data;
        stmt->param_bind[i].is_unsigned = 0;
        stmt->param_bind[i].length = 0;
        stmt->param_bind_data[i].is_null = 0;
        stmt->param_bind_data[i].length = 0;
    }
}

static void dbmysql_stmt_setuBigint (struct db_xsql_stmt_t *self, const int parameterIndex, const uint64_t value) {
    struct db_mysql_stmt_t *stmt = self->data;

    if (parameterIndex >= 1 && parameterIndex <= stmt->param_count) {
        int i = parameterIndex - 1;

        stmt->param_bind_data[i].uint64_data = value;
        stmt->param_bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
        stmt->param_bind[i].buffer = (char *) & (stmt->param_bind_data)[i].uint64_data;
        stmt->param_bind[i].is_unsigned = 1;
        stmt->param_bind[i].length = 0;
        stmt->param_bind_data[i].is_null = 0;
        stmt->param_bind_data[i].length = 0;
    }
}

static void dbmysql_stmt_setShort (struct db_xsql_stmt_t *self, const int parameterIndex, const short value) {
    struct db_mysql_stmt_t *stmt = self->data;

    if (parameterIndex >= 1 && parameterIndex <= stmt->param_count) {
        int i = parameterIndex - 1;

        stmt->param_bind_data[i].short_data = value;
        stmt->param_bind[i].buffer_type = MYSQL_TYPE_SHORT;
        stmt->param_bind[i].buffer = (char *) & (stmt->param_bind_data)[i].int_data;
        stmt->param_bind[i].is_unsigned = 0;
        stmt->param_bind[i].length = 0;
        stmt->param_bind_data[i].is_null = 0;
        stmt->param_bind_data[i].length = 0;
    }
}

static void dbmysql_stmt_setuInt (struct db_xsql_stmt_t *self, const int parameterIndex, const unsigned int value) {
    struct db_mysql_stmt_t *stmt = self->data;

    if (parameterIndex >= 1 && parameterIndex <= stmt->param_count) {
        int i = parameterIndex - 1;

        stmt->param_bind_data[i].uint_data = value;
        stmt->param_bind[i].buffer_type = MYSQL_TYPE_LONG;
        stmt->param_bind[i].buffer = (char *) & (stmt->param_bind_data)[i].int_data;
        stmt->param_bind[i].length = 0;
        stmt->param_bind[i].is_unsigned = 1;
        stmt->param_bind_data[i].is_null = 0;
        stmt->param_bind_data[i].length = 0;
    }
}

static void dbmysql_stmt_setInt (struct db_xsql_stmt_t *self, const int parameterIndex, const int value) {
    struct db_mysql_stmt_t *stmt = self->data;

    if (parameterIndex >= 1 && parameterIndex <= stmt->param_count) {
        int i = parameterIndex - 1;

        stmt->param_bind_data[i].int_data = value;
        stmt->param_bind[i].buffer_type = MYSQL_TYPE_LONG;
        stmt->param_bind[i].buffer = (char *) & (stmt->param_bind_data)[i].int_data;
        stmt->param_bind[i].is_unsigned = 0;
        stmt->param_bind[i].length = 0;
        stmt->param_bind_data[i].is_null = 0;
        stmt->param_bind_data[i].length = 0;
    }
}

static void dbmysql_stmt_setString (struct db_xsql_stmt_t *self, const int parameterIndex, const char *str) {
    struct db_mysql_stmt_t *stmt = self->data;

    if (parameterIndex >= 1 && parameterIndex <= stmt->param_count) {
        unsigned long len = strlen (str);
        int i = parameterIndex - 1;

        if (stmt->param_bind_data[i].str_data != NULL) {
            free (stmt->param_bind_data[i].str_data);
        }

        stmt->param_bind_data[i].str_data = strdup (str);
        stmt->param_bind_data[i].length = len;
        stmt->param_bind_data[i].is_null = 0;

        stmt->param_bind[i].buffer_type = MYSQL_TYPE_STRING;
        stmt->param_bind[i].buffer = (char *) stmt->param_bind_data[i].str_data;
        stmt->param_bind[i].length = & (stmt->param_bind_data[i].length);
        stmt->param_bind[i].buffer_length = len;
        stmt->param_bind[i].is_null = 0;
        // fprintf (stderr, "[%s]\n", (char *) stmt->param_bind[i].buffer);
    }
}

static void dbmysql_stmt_setNull (struct db_xsql_stmt_t *self, const int parameterIndex) {
    struct db_mysql_stmt_t *stmt = self->data;

    if (parameterIndex >= 1 && parameterIndex <= stmt->param_count) {
        (stmt->param_bind)[parameterIndex - 1].buffer_type = MYSQL_TYPE_NULL;
        (stmt->param_bind)[parameterIndex - 1].buffer = NULL;
        (stmt->param_bind)[parameterIndex - 1].buffer_length = 0;
    }
}

static struct db_xsql_stmt_t *dbmysql_createStatement (
    struct db_xsql_data_t *data,
    const char *statement) {
    struct db_mysql_data_t *self = (struct db_mysql_data_t *) data->data;
    struct db_xsql_stmt_t *st = malloc (sizeof (struct db_xsql_stmt_t));
    struct db_mysql_stmt_t *stmt = malloc (sizeof (struct db_mysql_stmt_t));
    int i;

    st->data = stmt;
    stmt->data = self;
    stmt->result_bind = NULL;
    stmt->result_bind_data = NULL;

    if ((stmt->statement = mysql_stmt_init (&self->mysql)) != NULL) {
        if (mysql_stmt_prepare (stmt->statement, statement, strlen (statement)) == 0) {
            stmt->param_count = mysql_stmt_param_count (stmt->statement);

            // fprintf (stderr, "%s(%d): parameter count = %d\n", __FILE__, __LINE__, stmt->param_count);
            if (stmt->param_count > 0) {
                stmt->param_bind = calloc (stmt->param_count, sizeof (MYSQL_BIND));
                stmt->param_bind_data = calloc (
                                            stmt->param_count,
                                            sizeof (struct db_mysql_result_bind_data_t)
                                        );

                for (i = 0; i < stmt->param_count; i++) {
                    (stmt->param_bind_data)[i].str_data = NULL;
                }
                dbmysql_clearParameters (st);
            } else {
                stmt->param_bind = NULL;
                stmt->param_bind_data = NULL;
            }

            st->close = dbmysql_stmt_close;
            st->executeQuery = dbmysql_executeQuery;
            st->executeUpdate = dbmysql_executeUpdate;
            st->executeMultipleQuery = dbmysql_executeMultipleQuery;
            st->clearParameters = dbmysql_clearParameters;
            // st->freeResult = dbmysql_stmt_free_result;
            st->setInt = dbmysql_stmt_setInt;
            st->setuInt = dbmysql_stmt_setuInt;
            st->setShort = dbmysql_stmt_setShort;
            st->setBigint = dbmysql_stmt_setBigint;
            st->setuBigint = dbmysql_stmt_setuBigint;
            st->setString = dbmysql_stmt_setString;
            st->setNull = dbmysql_stmt_setNull;
            return st;
        } else {
            logger->error (__FILE__, __LINE__, "%s (%s)", mysql_stmt_error (stmt->statement), statement);
            // mysql_stmt_close (stmt->statement);
        }
    }
    free (stmt);
    free (st);

    return NULL;
}

static void dbmysql_setLogger (struct logger_t *new_logger) {
    logger = new_logger;
}

static struct db_xsql_t db_mysql = {
    .newInstance = newInstance,
    .dispose = dispose,
    .connect = dbmysql_connect,
    .disconnect = dbmysql_disconnect,
    .errno = error_number,
    .error = error_string,
    .setInfo = dbmysql_setInfo,
    .createStatement = dbmysql_createStatement,
    .setLogger = dbmysql_setLogger
};

struct db_xsql_t *init_db_mysql (void) {
    return &db_mysql;
}

