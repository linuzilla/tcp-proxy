#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "logger.h"

struct db_connection_info_t;

enum db_xsql_field_types {
    XSQL_INT32,
    XSQL_INT64,
    XSQL_STRING
};

struct db_xsql_field_t {
    char			*fieldName;
    enum db_xsql_field_types fieldType;
};

struct db_xsql_data_t {
    void	*data;
};

struct db_xsql_result_t {
    void	*data;
    bool (*next) (struct db_xsql_result_t *result, unsigned int *errno);
    void (*close) (struct db_xsql_result_t *result);

    unsigned int  (*getuInt) (struct db_xsql_result_t *result, const int parameterIndex);
    int  (*getInt) (struct db_xsql_result_t *result, const int parameterIndex);
    short (*getShort) (struct db_xsql_result_t *result, const int parameterIndex);
    int64_t  (*getBigint) (struct db_xsql_result_t *result, const int parameterIndex);
    uint64_t (*getuBigint) (struct db_xsql_result_t *result, const int parameterIndex);
    char* (*getString) (struct db_xsql_result_t *result, const int parameterIndex);
    int	(*getIndex) (struct db_xsql_result_t *result, const char *str);
    time_t	(*getTimestamp) (struct db_xsql_result_t *result, const int parameterIndex);

    struct db_xsql_field_t * (*getFields) (struct db_xsql_result_t *self);
    unsigned int (*getNumberOfFields) (struct db_xsql_result_t *self);
};

struct db_xsql_stmt_t {
    void	*data;
    struct db_xsql_result_t * (*executeQuery) (struct db_xsql_stmt_t *self, int *errcode);
    int (*executeUpdate) (struct db_xsql_stmt_t *self, int *errcode);
    bool (*executeMultipleQuery) (struct db_xsql_stmt_t *self, int *errcode, void *padLoad, void (*result_handler) (struct db_xsql_result_t *, void *padLoad));

    void (*clearParameters) (struct db_xsql_stmt_t *self);
    void (*freeResult) (struct db_xsql_stmt_t *self);
    void (*setuInt) (struct db_xsql_stmt_t *self, const int parameterIndex, const unsigned int value);
    void (*setInt) (struct db_xsql_stmt_t *self, const int parameterIndex, const int value);
    void (*setShort) (struct db_xsql_stmt_t *self, const int parameterIndex, const short value);
    void (*setString) (struct db_xsql_stmt_t *self, const int parameterIndex, const char *str);
    void (*setuBigint) (struct db_xsql_stmt_t *self, const int parameterIndex, const uint64_t value);
    void (*setBigint) (struct db_xsql_stmt_t *self, const int parameterIndex, const int64_t value);
    void (*setNull) (struct db_xsql_stmt_t *self, const int parameterIndex);
    void (*close) (struct db_xsql_stmt_t *self);

};

struct db_xsql_t {
    struct db_xsql_data_t * (*newInstance) (void);
    void			(*dispose) (struct db_xsql_data_t *self);

    void			(*setInfo) (struct db_xsql_data_t *self,
                                struct db_connection_info_t *info);

    bool			(*connect) (struct db_xsql_data_t *self);
    void			(*disconnect) (struct db_xsql_data_t *self);
    struct db_xsql_stmt_t *	(*createStatement) (struct db_xsql_data_t *self, const char *statement);

    void			(*setLogger) (struct logger_t *logger);

    const char *    (*error) (struct db_xsql_data_t *self);
    int             (*errno) (struct db_xsql_data_t *self);
};
