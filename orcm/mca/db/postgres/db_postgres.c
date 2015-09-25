/*
 * Copyright (c) 2012-2013 Los Alamos National Security, Inc. All rights reserved.
 * Copyright (c) 2013-2015 Intel, Inc. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 */

#include "orcm_config.h"
#include "orcm/constants.h"

#include <string.h>
#include <sys/types.h>
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#include <stdio.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <sys/time.h>
#include <time.h>

#include "opal_stdint.h"
#include "opal/util/argv.h"
#include "opal/util/error.h"
#include "opal/class/opal_bitmap.h"

#include "orcm/runtime/orcm_globals.h"

#include "orcm/mca/db/base/base.h"
#include "db_postgres.h"

#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef PACKAGE_URL

#include "libpq-fe.h"
#include "postgres_fe.h"
#include "catalog/pg_type.h"

#define ORCM_PG_MAX_LINE_LENGTH 4096

static int postgres_init(struct orcm_db_base_module_t *imod);
static void postgres_finalize(struct orcm_db_base_module_t *imod);
static int postgres_store_sample(struct orcm_db_base_module_t *imod,
                                 const char *data_group,
                                 opal_list_t *kvs);
static int postgres_store(struct orcm_db_base_module_t *imod,
                          orcm_db_data_type_t data_type,
                          opal_list_t *input,
                          opal_list_t *ret);
static int postgres_update_node_features(struct orcm_db_base_module_t *imod,
                                         const char *hostname,
                                         opal_list_t *features);
static int postgres_record_diag_test(struct orcm_db_base_module_t *imod,
                                     const char *hostname,
                                     const char *diag_type,
                                     const char *diag_subtype,
                                     const struct tm *start_time,
                                     const struct tm *end_time,
                                     const int *component_index,
                                     const char *test_result,
                                     opal_list_t *test_params);
static int postgres_commit(struct orcm_db_base_module_t *imod);
static int postgres_rollback(struct orcm_db_base_module_t *imod);

/* Internal helper functions */
static int postgres_store_data_sample(mca_db_postgres_module_t *mod,
                                      opal_list_t *input,
                                      opal_list_t *ret);
static int postgres_store_node_features(mca_db_postgres_module_t *mod,
                                        opal_list_t *input,
                                        opal_list_t *ret);
static int postgres_store_diag_test(mca_db_postgres_module_t *mod,
                                    opal_list_t *input,
                                    opal_list_t *ret);

static bool tv_to_str_time_stamp(const struct timeval *time, char *tbuf,
                                 size_t size);
static void tm_to_str_time_stamp(const struct tm *time, char *tbuf,
                                 size_t size);
static inline bool status_ok(PGresult *res);

static int postgres_fetch(struct orcm_db_base_module_t *imod,
                          const char *view,
                          opal_list_t *filters,
                          opal_list_t *output);

static int postgres_get_num_rows(struct orcm_db_base_module_t *imod,
                                 int rshandle,
                                 int *num_rows);

static int postgres_get_next_row(struct orcm_db_base_module_t *imod,
                                 int rshandle,
                                 opal_list_t *row);

static int postgres_close_result_set(struct orcm_db_base_module_t *imod,
                                     int rshandle);

mca_db_postgres_module_t mca_db_postgres_module = {
    {
        postgres_init,
        postgres_finalize,
        postgres_store_sample,
        postgres_store,
        NULL,
        postgres_update_node_features,
        postgres_record_diag_test,
        postgres_commit,
        postgres_rollback,
        postgres_fetch,
        postgres_get_num_rows,
        postgres_get_next_row,
        postgres_close_result_set,
        NULL
    },
};

#define ERROR_MSG_FMT_INIT(mod, msg, ...) \
    opal_output(0, "***********************************************"); \
    opal_output(0, "db:postgres: Connection failed: "); \
    opal_output(0, msg, ##__VA_ARGS__); \
    opal_output(0, "\tURI: %s", mod->pguri); \
    opal_output(0, "\tDB: %s", mod->dbname); \
    opal_output(0, "\tOptions: %s", \
                NULL == mod->pgoptions ? "NULL" : mod->pgoptions); \
    opal_output(0, "\tTTY: %s", NULL == mod->pgtty ? "NULL" : mod->pgtty); \
    opal_output(0, "***********************************************");

static int postgres_init(struct orcm_db_base_module_t *imod)
{
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;
    char **login = NULL;
    char **uri_parts = NULL;
    char *host;
    char *port;
    int count;
    int i;
    PGconn *conn;

    mod->results_sets = OBJ_NEW(opal_pointer_array_t);

    if (NULL == mod->dbname) {
        opal_output(0, "db:postgres: No database name provided");
        return ORCM_ERR_BAD_PARAM;
    }

    /* break the user info into its login parts */
    login = opal_argv_split(mod->user, ':');
    if (2 != opal_argv_count(login)) {
        opal_output(0, "db:postgres: User info is invalid: %s",
                    mod->user);
        opal_argv_free(login);
        return ORCM_ERR_BAD_PARAM;
    }
    /* break the URI */
    uri_parts = opal_argv_split(mod->pguri, ':');
    count = opal_argv_count(uri_parts);
    if (2 < count || 1 > count) {
        opal_output(0, "db:postgres: URI info is invalid: %s",
                    mod->pguri);
        opal_argv_free(login);
        opal_argv_free(uri_parts);
        return ORCM_ERR_BAD_PARAM;
    }
    host = uri_parts[0];
    port = count > 1 ? uri_parts[1] : NULL;

    conn = PQsetdbLogin(host, port,
                        mod->pgoptions,
                        mod->pgtty,
                        mod->dbname,
                        login[0], login[1]);
    opal_argv_free(login);
    opal_argv_free(uri_parts);

    if (PQstatus(conn) != CONNECTION_OK) {
        ERROR_MSG_FMT_INIT(mod, "%s", PQerrorMessage(conn));

        return ORCM_ERR_CONNECTION_FAILED;
    }

    mod->conn = conn;

    for (i = 0; i < ORCM_DB_PG_STMT_NUM_STMTS; i++) {
        mod->prepared[i] = false;
    }

    opal_output_verbose(5, orcm_db_base_framework.framework_output,
                        "db:postgres: Connection established to %s",
                        mod->dbname);

    return ORCM_SUCCESS;
}

static void postgres_finalize(struct orcm_db_base_module_t *imod)
{
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;

    if (NULL != mod->pguri) {
        free(mod->pguri);
    }
    if (NULL != mod->dbname) {
        free(mod->dbname);
    }
    if (NULL != mod->user) {
        free(mod->user);
    }
    if (NULL != mod->pgoptions) {
        free(mod->pgoptions);
    }
    if (NULL != mod->pgtty) {
        free(mod->pgtty);
    }
    if (NULL != mod->conn) {
        PQfinish(mod->conn);
    }
    OBJ_RELEASE(mod->results_sets);
    mod->results_sets = NULL;
    free(mod);
}

#define ERR_MSG_STORE(msg) \
    opal_output(0, "***********************************************"); \
    opal_output(0, "db:postgres: Unable to record data sample: "); \
    opal_output(0, msg); \
    opal_output(0, "***********************************************");

#define ERR_MSG_FMT_STORE(msg, ...) \
    opal_output(0, "***********************************************"); \
    opal_output(0, "db:postgres: Unable to record data sample: "); \
    opal_output(0, msg, ##__VA_ARGS__); \
    opal_output(0, "***********************************************");

static int postgres_store(struct orcm_db_base_module_t *imod,
                          orcm_db_data_type_t data_type,
                          opal_list_t *input,
                          opal_list_t *ret)
{
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;
    int rc = ORCM_SUCCESS;

    switch (data_type) {
    case ORCM_DB_ENV_DATA:
        rc = postgres_store_data_sample(mod, input, ret);
        break;
    case ORCM_DB_INVENTORY_DATA:
        rc = postgres_store_node_features(mod, input, ret);
        break;
    case ORCM_DB_DIAG_DATA:
        rc = postgres_store_diag_test(mod, input, ret);
        break;
    default:
        return ORCM_ERR_NOT_IMPLEMENTED;
    }

    return rc;
}

static int postgres_store_sample(struct orcm_db_base_module_t *imod,
                                 const char *data_group,
                                 opal_list_t *kvs)
{
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;
    int rc = ORCM_SUCCESS;

    opal_value_t *kv;
    opal_value_t *timestamp_item = NULL;
    opal_value_t *hostname_item = NULL;

    char hostname[256];
    char time_stamp[40];
    char **data_item_parts=NULL;
    char *data_item;
    orcm_db_item_t item;
    char *units;
    int count;
    int ret;
    size_t num_items;
    char **rows = NULL;
    char *values = NULL;
    char *insert_stmt = NULL;
    size_t i;

    PGresult *res = NULL;

    if (NULL == data_group) {
        ERR_MSG_STORE("No data group specified");
        return ORCM_ERR_BAD_PARAM;
    }

    if (NULL == kvs) {
        ERR_MSG_STORE("No value list specified");
        return ORCM_ERR_BAD_PARAM;
    }

    /* First, retrieve the time stamp and the hostname from the list */
    OPAL_LIST_FOREACH(kv, kvs, opal_value_t) {
        if (!strcmp(kv->key, "ctime")) {
            switch (kv->type) {
            case OPAL_TIMEVAL:
            case OPAL_TIME:
                if (!tv_to_str_time_stamp(&kv->data.tv, time_stamp,
                                          sizeof(time_stamp))) {
                    ERR_MSG_STORE("Failed to convert time stamp value");
                    return ORCM_ERR_BAD_PARAM;
                }
                break;
            case OPAL_STRING:
                strncpy(time_stamp, kv->data.string, sizeof(time_stamp) - 1);
                time_stamp[sizeof(time_stamp) - 1] = '\0';
                break;
            default:
                ERR_MSG_STORE("Invalid value type specified for time stamp");
                return ORCM_ERR_BAD_PARAM;
            }
            timestamp_item = kv;
        } else if (!strcmp(kv->key, "hostname")) {
            if (OPAL_STRING == kv->type) {
                strncpy(hostname, kv->data.string, sizeof(hostname) - 1);
                hostname[sizeof(hostname) - 1] = '\0';
            } else {
                ERR_MSG_STORE("Invalid value type specified for hostname");
                return ORCM_ERR_BAD_PARAM;
            }
            hostname_item = kv;
        }

        if (NULL != timestamp_item && NULL != hostname_item) {
            break;
        }
    }

    if (NULL == timestamp_item) {
        ERR_MSG_STORE("No time stamp provided");
        return ORCM_ERR_BAD_PARAM;
    }
    if (NULL == hostname_item) {
        ERR_MSG_STORE("No hostname provided");
        return ORCM_ERR_BAD_PARAM;
    }

    /* Remove these from the list to avoid processing them again */
    opal_list_remove_item(kvs, (opal_list_item_t *)timestamp_item);
    opal_list_remove_item(kvs, (opal_list_item_t *)hostname_item);
    OBJ_RELEASE(timestamp_item);
    OBJ_RELEASE(hostname_item);

    num_items = opal_list_get_size(kvs);
    rows = (char **)malloc(sizeof(char *) * (num_items + 1));
    if (NULL == rows) {
        rc = ORCM_ERR_OUT_OF_RESOURCE;
        ERR_MSG_STORE("Unable to allocate memory");
        goto cleanup_and_exit;
    }
    for (i = 0; i < num_items + 1; i++) {
        rows[i] = NULL;
    }
    i = 0;
    OPAL_LIST_FOREACH(kv, kvs, opal_value_t) {
        /* kv->key will contain: <data_item>:<units> */
        data_item_parts = opal_argv_split(kv->key, ':');
        count = opal_argv_count(data_item_parts);
        if (count > 0) {
            data_item = data_item_parts[0];
            if (count > 1) {
                units = data_item_parts[1];
            } else {
                units = NULL;
            }
        } else {
            rc = ORCM_ERR_BAD_PARAM;
            ERR_MSG_STORE("No data item specified");
            goto cleanup_and_exit;
        }

        ret = opal_value_to_orcm_db_item(kv, &item);
        if (ORCM_SUCCESS != ret) {
            rc = ORCM_ERR_NOT_SUPPORTED;
            ERR_MSG_FMT_STORE("Unsupported data type: %s",
                              opal_dss.lookup_data_type(kv->type));
            goto cleanup_and_exit;
        }

        /* (hostname,
         *  data_item,
         *  time_stamp,
         *  value_int,
         *  value_real,
         *  value_str,
         *  units,
         *  data_type_id) */
        switch (item.item_type) {
        case ORCM_DB_ITEM_STRING:
            if (NULL != units) {
                asprintf(rows + i,
                         "('%s','%s_%s','%s',NULL,NULL,'%s','%s',%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_str, units, kv->type);
            } else {
                asprintf(rows + i,
                         "('%s','%s_%s','%s',NULL,NULL,'%s',NULL,%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_str, kv->type);
            }
            break;
        case ORCM_DB_ITEM_REAL:
            if (NULL != units) {
                asprintf(rows + i,
                         "('%s','%s_%s','%s',NULL,%f,NULL,'%s',%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_real, units, kv->type);
            } else {
                asprintf(rows + i,
                         "('%s','%s_%s','%s',NULL,%f,NULL,NULL,%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_real, kv->type);
            }
            break;
        default: /* ORCM_DB_ITEM_INTEGER */
            if (NULL != units) {
                asprintf(rows + i,
                         "('%s','%s_%s','%s',%lld,NULL,NULL,'%s',%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_int, units, kv->type);
            } else {
                asprintf(rows + i,
                         "('%s','%s_%s','%s',%lld,NULL,NULL,NULL,%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_int, kv->type);
            }
        }
        i++;
        if (NULL != data_item_parts){
            opal_argv_free(data_item_parts);
            data_item_parts = NULL;
        }
    }

    values = opal_argv_join(rows, ',');
    opal_argv_free(rows);
    rows = NULL;

    asprintf(&insert_stmt, "insert into data_sample_raw(hostname,data_item,"
             "time_stamp,value_int,value_real,value_str,units,data_type_id) "
             "values %s", values);
    free(values);
    values = NULL;

    /* If we're not in auto commit mode, let's start a new transaction (if
     * one hasn't already been started) */
    if (!mod->tran_started && !mod->autocommit) {
        res = PQexec(mod->conn, "begin");
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_FMT_STORE("Unable to start transaction: %s",
                              PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;
        mod->tran_started = true;
    }

    res = PQexec(mod->conn, insert_stmt);
    if (!status_ok(res)) {
        rc = ORCM_ERROR;
        ERR_MSG_STORE(PQresultErrorMessage(res));
        goto cleanup_and_exit;
    }
    PQclear(res);
    res = NULL;

    opal_output_verbose(2, orcm_db_base_framework.framework_output,
                        "postgres_store_sample succeeded");

cleanup_and_exit:
    if (NULL != res) {
        PQclear(res);
    }

    if (NULL != rows) {
        opal_argv_free(rows);
    }

    if (NULL != insert_stmt) {
        free(insert_stmt);
    }
    if (NULL != data_item_parts){
        opal_argv_free(data_item_parts);
    }

    return rc;
}

static int postgres_store_data_sample(mca_db_postgres_module_t *mod,
                                      opal_list_t *input,
                                      opal_list_t *ret)
{
    int rc = ORCM_SUCCESS;

    const int NUM_PARAMS = 3;
    const char *params[] = {
        "data_group",
        "ctime",
        "hostname"
    };
    opal_value_t *param_items[] = {NULL, NULL, NULL};
    opal_bitmap_t item_bm;

    char *hostname = NULL;
    char *data_group = NULL;
    char time_stamp[40];
    char *data_item;
    orcm_db_item_t item;
    char *units;

    size_t num_items;
    char **rows = NULL;
    char *values = NULL;
    char *insert_stmt = NULL;
    size_t i, j;

    opal_value_t *kv;
    orcm_metric_value_t *mv;

    PGresult *res = NULL;

    if (NULL == input) {
        ERR_MSG_STORE("No parameters provided");
        return ORCM_ERR_BAD_PARAM;
    }

    num_items = opal_list_get_size(input);
    OBJ_CONSTRUCT(&item_bm, opal_bitmap_t);
    opal_bitmap_init(&item_bm, (int)num_items);

    /* Get the main parameters form the list */
    find_items(params, NUM_PARAMS, input, param_items, &item_bm);

    /* Check the parameters */
    if (NULL == param_items[0]) {
        ERR_MSG_STORE("No data group provided");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    if (NULL == param_items[1]) {
        ERR_MSG_STORE("No time stamp provided");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }
    if (NULL == param_items[2]) {
        ERR_MSG_STORE("No hostname provided");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    kv = param_items[0];
    if (OPAL_STRING == kv->type) {
        data_group = kv->data.string;
    } else {
        ERR_MSG_STORE("Invalid value type specified for data group");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    kv = param_items[1];
    switch (kv->type) {
    case OPAL_TIMEVAL:
    case OPAL_TIME:
        if (!tv_to_str_time_stamp(&kv->data.tv, time_stamp,
                                  sizeof(time_stamp))) {
            ERR_MSG_STORE("Failed to convert time stamp value");
            rc = ORCM_ERR_BAD_PARAM;
            goto cleanup_and_exit;
        }
        break;
    case OPAL_STRING:
        strncpy(time_stamp, kv->data.string, sizeof(time_stamp) - 1);
        time_stamp[sizeof(time_stamp) - 1] = '\0';
        break;
    default:
        ERR_MSG_STORE("Invalid value type specified for time stamp");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    kv = param_items[2];
    if (OPAL_STRING == kv->type) {
        hostname = kv->data.string;
    } else {
        ERR_MSG_STORE("Invalid value type specified for hostname");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    num_items -= NUM_PARAMS;
    if (num_items <= 0) {
        ERR_MSG_STORE("No data samples provided");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    rows = (char **)malloc(sizeof(char *) * (num_items + 1));
    if (NULL == rows) {
        rc = ORCM_ERR_OUT_OF_RESOURCE;
        ERR_MSG_STORE("Unable to allocate memory");
        goto cleanup_and_exit;
    }
    for (i = 0; i < num_items + 1; i++) {
        rows[i] = NULL;
    }

    /* Build the SQL command with the data provided in the list */
    i = 0;
    j = 0;
    OPAL_LIST_FOREACH(mv, input, orcm_metric_value_t) {
        /* Ignore the items that have already been processed */
        if (opal_bitmap_is_set_bit(&item_bm, i)) {
            i++;
            continue;
        }

        data_item = mv->value.key;
        units = mv->units;
        if (NULL == data_item) {
            rc = ORCM_ERR_BAD_PARAM;
            ERR_MSG_STORE("No data item specified");
            goto cleanup_and_exit;
        }

        if (ORCM_SUCCESS != opal_value_to_orcm_db_item(&mv->value, &item)) {
            rc = ORCM_ERR_NOT_SUPPORTED;
            ERR_MSG_FMT_STORE("Unsupported data type: %s",
                              opal_dss.lookup_data_type(mv->value.type));
            goto cleanup_and_exit;
        }

        /* (hostname,
         *  data_item,
         *  time_stamp,
         *  value_int,
         *  value_real,
         *  value_str,
         *  units,
         *  data_type_id) */
        switch (item.item_type) {
        case ORCM_DB_ITEM_STRING:
            if (NULL != units) {
                asprintf(rows + j,
                         "('%s','%s_%s','%s',NULL,NULL,'%s','%s',%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_str, units, mv->value.type);
            } else {
                asprintf(rows + j,
                         "('%s','%s_%s','%s',NULL,NULL,'%s',NULL,%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_str, mv->value.type);
            }
            break;
        case ORCM_DB_ITEM_REAL:
            if (NULL != units) {
                asprintf(rows + j,
                         "('%s','%s_%s','%s',NULL,%f,NULL,'%s',%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_real, units, mv->value.type);
            } else {
                asprintf(rows + j,
                         "('%s','%s_%s','%s',NULL,%f,NULL,NULL,%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_real, mv->value.type);
            }
            break;
        default: /* ORCM_DB_ITEM_INTEGER */
            if (NULL != units) {
                asprintf(rows + j,
                         "('%s','%s_%s','%s',%lld,NULL,NULL,'%s',%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_int, units, mv->value.type);
            } else {
                asprintf(rows + j,
                         "('%s','%s_%s','%s',%lld,NULL,NULL,NULL,%d)",
                         hostname, data_group, data_item, time_stamp,
                         item.value.value_int, mv->value.type);
            }
        }
        i++;
        j++;
    }

    values = opal_argv_join(rows, ',');
    opal_argv_free(rows);
    rows = NULL;

    asprintf(&insert_stmt, "insert into data_sample_raw(hostname,data_item,"
             "time_stamp,value_int,value_real,value_str,units,data_type_id) "
             "values %s", values);
    free(values);
    values = NULL;

    /* If we're not in auto commit mode, let's start a new transaction (if
     * one hasn't already been started) */
    if (!mod->tran_started && !mod->autocommit) {
        res = PQexec(mod->conn, "begin");
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_FMT_STORE("Unable to start transaction: %s",
                              PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;
        mod->tran_started = true;
    }

    res = PQexec(mod->conn, insert_stmt);
    if (!status_ok(res)) {
        rc = ORCM_ERROR;
        ERR_MSG_STORE(PQresultErrorMessage(res));
        goto cleanup_and_exit;
    }
    PQclear(res);
    res = NULL;

    opal_output_verbose(2, orcm_db_base_framework.framework_output,
                        "postgres_store_sample succeeded");

cleanup_and_exit:
    if (NULL != res) {
        PQclear(res);
    }

    if (NULL != rows) {
        opal_argv_free(rows);
    }

    if (NULL != insert_stmt) {
        free(insert_stmt);
    }

    OBJ_DESTRUCT(&item_bm);
    return rc;
}

#define ERR_MSG_UNF(msg) \
    opal_output(0, "***********************************************"); \
    opal_output(0, "db:postgres: Unable to update node features"); \
    opal_output(0, msg); \
    opal_output(0, "***********************************************");

#define ERR_MSG_FMT_UNF(msg, ...) \
    opal_output(0, "***********************************************"); \
    opal_output(0, "db:postgres: Unable to update node features"); \
    opal_output(0, msg, ##__VA_ARGS__); \
    opal_output(0, "***********************************************");

static int postgres_update_node_features(struct orcm_db_base_module_t *imod,
                                         const char *hostname,
                                         opal_list_t *features)
{
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;
    int rc = ORCM_SUCCESS;

    orcm_metric_value_t *mv;

    const int NUM_PARAMS = 7;
    const char *params[NUM_PARAMS];
    char *type_str = NULL;
    char *value_str = NULL;
    orcm_db_item_t item;
    int ret;

    PGresult *res = NULL;

    bool local_tran_started = false;

    if (NULL == hostname) {
        ERR_MSG_UNF("No hostname provided");
        return ORCM_ERR_BAD_PARAM;
    }

    if (NULL == features) {
        ERR_MSG_UNF("No node features provided");
        return ORCM_ERR_BAD_PARAM;
    }

    /* Prepare the statement only if it hasn't already been prepared */
    if (!mod->prepared[ORCM_DB_PG_STMT_SET_NODE_FEATURE]) {
        /*
         * $1: p_hostname character varying,
         * $2: p_feature character varying,
         * $3: p_data_type_id integer,
         * $4: p_value_int bigint,
         * $5: p_value_real double precision,
         * $6: p_value_str character varying,
         * $7: p_units character varying
         * */
        res = PQprepare(mod->conn, "set_node_feature",
                        "select set_node_feature($1, $2, $3, $4, $5, $6, $7)",
                        0, NULL);
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_UNF(PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        mod->prepared[ORCM_DB_PG_STMT_SET_NODE_FEATURE] = true;
    }

    if (mod->autocommit || !mod->tran_started) {
        res = PQexec(mod->conn, "begin");
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_FMT_UNF("Unable to begin transaction: %s",
                            PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        local_tran_started = true;
    }

    params[0] = hostname;
    OPAL_LIST_FOREACH(mv, features, orcm_metric_value_t) {
        if (NULL == mv->value.key || 0 == strlen(mv->value.key)) {
            rc = ORCM_ERR_BAD_PARAM;
            ERR_MSG_UNF("Key or node feature name not provided for value");
            goto cleanup_and_exit;
        }

        ret = opal_value_to_orcm_db_item(&mv->value, &item);
        if (ORCM_SUCCESS != ret) {
            rc = ORCM_ERR_NOT_SUPPORTED;
            ERR_MSG_FMT_UNF("Unsupported data type: %s",
                            opal_dss.lookup_data_type(mv->value.type));
            goto cleanup_and_exit;
        }

        params[1] = mv->value.key;
        asprintf(&type_str, "%d", item.opal_type);
        params[2] = type_str;
        switch (item.item_type) {
        case ORCM_DB_ITEM_STRING:
            params[3] = NULL;
            params[4] = NULL;
            params[5] = item.value.value_str;
            break;
        case ORCM_DB_ITEM_REAL:
            params[3] = NULL;
            asprintf(&value_str, "%f", item.value.value_real);
            params[4] = value_str;
            params[5] = NULL;
            break;
        default:
            asprintf(&value_str, "%lld", item.value.value_int);
            params[3] = value_str;
            params[4] = NULL;
            params[5] = NULL;
        }
        params[6] = mv->units;

        res = PQexecPrepared(mod->conn, "set_node_feature", NUM_PARAMS,
                             params, NULL, NULL, 0);
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_UNF(PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        free(type_str);
        type_str = NULL;
        if (NULL != value_str) {
            free(value_str);
            value_str = NULL;
        }
    }

    if (mod->autocommit) {
        res = PQexec(mod->conn, "commit");
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_FMT_UNF("Unable to commit transaction: %s",
                            PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;
    }

    opal_output_verbose(2, orcm_db_base_framework.framework_output,
                        "postgres_update_node_features succeeded");

cleanup_and_exit:
    /* If we're in auto commit mode, then make sure our local changes
     * are either committed or canceled. */
    if (ORCM_SUCCESS != rc && mod->autocommit && local_tran_started) {
        res = PQexec(mod->conn, "rollback");
    }

    if (NULL != res) {
        PQclear(res);
    }

    if (NULL != type_str) {
        free(type_str);
    }

    if (NULL != value_str) {
        free(value_str);
    }

    return rc;
}

static int postgres_store_node_features(mca_db_postgres_module_t *mod,
                                        opal_list_t *input,
                                        opal_list_t *ret)
{
    int rc = ORCM_SUCCESS;

    const int NUM_PARAMS = 1;
    const char *params[] = {
        "hostname"
    };
    opal_value_t *param_items[] = {NULL};
    opal_bitmap_t item_bm;

    char *hostname = NULL;

    const int SP_NUM_PARAMS = 7;
    const char *sp_params[SP_NUM_PARAMS];
    char *type_str = NULL;
    char *value_str = NULL;
    orcm_db_item_t item;
    size_t num_items;

    orcm_metric_value_t *mv;
    opal_value_t *kv;
    int i;

    PGresult *res = NULL;

    bool local_tran_started = false;

    if (NULL == input) {
        ERR_MSG_UNF("No parameters provided");
        return ORCM_ERR_BAD_PARAM;
    }

    num_items = opal_list_get_size(input);
    OBJ_CONSTRUCT(&item_bm, opal_bitmap_t);
    opal_bitmap_init(&item_bm, (int)num_items);

    /* Get the main parameters from the list */
    find_items(params, NUM_PARAMS, input, param_items, &item_bm);

    /* Check parameters */
    if (NULL == param_items[0]) {
        ERR_MSG_UNF("No hostname provided");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    kv = param_items[0];
    if (OPAL_STRING == kv->type) {
        hostname = kv->data.string;
    } else {
        ERR_MSG_UNF("Invalid value type specified for hostname");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    if (num_items <= (size_t)NUM_PARAMS) {
        ERR_MSG_UNF("No node features provided");
        goto cleanup_and_exit;
    }

    /* Prepare the statement only if it hasn't already been prepared */
    if (!mod->prepared[ORCM_DB_PG_STMT_SET_NODE_FEATURE]) {
        /*
         * $1: p_hostname character varying,
         * $2: p_feature character varying,
         * $3: p_data_type_id integer,
         * $4: p_value_int bigint,
         * $5: p_value_real double precision,
         * $6: p_value_str character varying,
         * $7: p_units character varying
         * */
        res = PQprepare(mod->conn, "set_node_feature",
                        "select set_node_feature($1, $2, $3, $4, $5, $6, $7)",
                        0, NULL);
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_UNF(PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        mod->prepared[ORCM_DB_PG_STMT_SET_NODE_FEATURE] = true;
    }

    if (mod->autocommit || !mod->tran_started) {
        res = PQexec(mod->conn, "begin");
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_FMT_UNF("Unable to begin transaction: %s",
                            PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        local_tran_started = true;
    }

    /* Build and execute the SQL commands to store the data in the list */
    sp_params[0] = hostname;
    i = 0;
    OPAL_LIST_FOREACH(mv, input, orcm_metric_value_t) {
        /* Ignore the items that have already been processed. */
        if (opal_bitmap_is_set_bit(&item_bm, i)) {
            i++;
            continue;
        }

        if (NULL == mv->value.key || 0 == strlen(mv->value.key)) {
            rc = ORCM_ERR_BAD_PARAM;
            ERR_MSG_UNF("Key or node feature name not provided for value");
            goto cleanup_and_exit;
        }

        if (ORCM_SUCCESS != opal_value_to_orcm_db_item(&mv->value, &item)) {
            rc = ORCM_ERR_NOT_SUPPORTED;
            ERR_MSG_FMT_UNF("Unsupported data type: %s",
                            opal_dss.lookup_data_type(mv->value.type));
            goto cleanup_and_exit;
        }

        sp_params[1] = mv->value.key;
        asprintf(&type_str, "%d", item.opal_type);
        sp_params[2] = type_str;
        switch (item.item_type) {
        case ORCM_DB_ITEM_STRING:
            sp_params[3] = NULL;
            sp_params[4] = NULL;
            sp_params[5] = item.value.value_str;
            break;
        case ORCM_DB_ITEM_REAL:
            sp_params[3] = NULL;
            asprintf(&value_str, "%f", item.value.value_real);
            sp_params[4] = value_str;
            sp_params[5] = NULL;
            break;
        default:
            asprintf(&value_str, "%lld", item.value.value_int);
            sp_params[3] = value_str;
            sp_params[4] = NULL;
            sp_params[5] = NULL;
        }
        sp_params[6] = mv->units;

        res = PQexecPrepared(mod->conn, "set_node_feature", SP_NUM_PARAMS,
                             sp_params, NULL, NULL, 0);
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_UNF(PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        free(type_str);
        type_str = NULL;
        if (NULL != value_str) {
            free(value_str);
            value_str = NULL;
        }
        i++;
    }

    if (mod->autocommit) {
        res = PQexec(mod->conn, "commit");
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_FMT_UNF("Unable to commit transaction: %s",
                            PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;
    }

    opal_output_verbose(2, orcm_db_base_framework.framework_output,
                        "postgres_update_node_features succeeded");

cleanup_and_exit:
    /* If we're in auto commit mode, then make sure our local changes
     * are either committed or canceled. */
    if (ORCM_SUCCESS != rc && mod->autocommit && local_tran_started) {
        res = PQexec(mod->conn, "rollback");
    }

    if (NULL != res) {
        PQclear(res);
    }

    if (NULL != type_str) {
        free(type_str);
    }

    if (NULL != value_str) {
        free(value_str);
    }

    OBJ_DESTRUCT(&item_bm);
    return rc;
}

#define ERR_MSG_RDT(msg) \
    opal_output(0, "***********************************************"); \
    opal_output(0, "db:postgres: Unable to record diagnostic test: "); \
    opal_output(0, msg); \
    opal_output(0, "***********************************************");

#define ERR_MSG_FMT_RDT(msg, ...) \
    opal_output(0, "***********************************************"); \
    opal_output(0, "db:postgres: Unable to record diagnostic test: "); \
    opal_output(0, msg, ##__VA_ARGS__); \
    opal_output(0, "***********************************************");

static int postgres_record_diag_test(struct orcm_db_base_module_t *imod,
                                     const char *hostname,
                                     const char *diag_type,
                                     const char *diag_subtype,
                                     const struct tm *start_time,
                                     const struct tm *end_time,
                                     const int *component_index,
                                     const char *test_result,
                                     opal_list_t *test_params)
{
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;
    int rc = ORCM_SUCCESS;

    const int NUM_TEST_RESULT_PARAMS = 7;
    const int NUM_TEST_CONFIG_PARAMS = 10;

    const char *result_params[NUM_TEST_RESULT_PARAMS];
    const char *config_params[NUM_TEST_CONFIG_PARAMS];
    char *index_str = NULL;
    char *type_str = NULL;
    char *value_str = NULL;
    char start_time_str[40];
    char end_time_str[40];

    orcm_metric_value_t *mv;
    orcm_db_item_t item;

    PGresult *res = NULL;
    int ret;

    bool local_tran_started = false;

    if (NULL == hostname) {
        ERR_MSG_RDT("No hostname provided");
        return ORCM_ERR_BAD_PARAM;
    }

    if (NULL == diag_type) {
        ERR_MSG_RDT("No diagnostic type provided");
        return ORCM_ERR_BAD_PARAM;
    }

    if (NULL == diag_subtype) {
        ERR_MSG_RDT("No diagnostic subtype provided");
        return ORCM_ERR_BAD_PARAM;
    }

    if (NULL == start_time) {
        ERR_MSG_RDT("No start time provided");
        return ORCM_ERR_BAD_PARAM;
    }

    if (NULL == test_result) {
        ERR_MSG_RDT("No test result provided");
        return ORCM_ERR_BAD_PARAM;
    }

    /* Prepare the statement only if it hasn't already been prepared */
    if (!mod->prepared[ORCM_DB_PG_STMT_RECORD_DIAG_TEST_RESULT]) {
        /*
         * $1: hostname
         * $2: diagnostic type
         * $3: diagnostic subtype
         * $4: start time
         * $5: end time
         * $6: component index
         * $7: test result
         */
        res = PQprepare(mod->conn, "record_diag_test_result",
                        "select record_diag_test_result("
                        "$1, $2, $3, $4, $5, $6, $7)",
                        0, NULL);
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_RDT(PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        mod->prepared[ORCM_DB_PG_STMT_RECORD_DIAG_TEST_RESULT] = true;
    }

    if (mod->autocommit || !mod->tran_started) {
        res = PQexec(mod->conn, "begin");
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_FMT_RDT("Unable to begin transaction: %s",
                            PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        local_tran_started = true;
    }

    tm_to_str_time_stamp(start_time, start_time_str, sizeof(start_time_str));

    if (NULL == component_index) {
        index_str = NULL;
    } else {
        asprintf(&index_str, "%d", *component_index);
    }

    result_params[0] = hostname;
    result_params[1] = diag_type;
    result_params[2] = diag_subtype;
    result_params[3] = start_time_str;
    if (NULL != end_time) {
        tm_to_str_time_stamp(end_time, end_time_str, sizeof(end_time_str));
        result_params[4] = end_time_str;
    } else {
        result_params[4] = NULL;
    }
    result_params[5] = index_str;
    result_params[6] = test_result;

    res = PQexecPrepared(mod->conn, "record_diag_test_result",
                         NUM_TEST_RESULT_PARAMS, result_params, NULL, NULL, 0);
    if (!status_ok(res)) {
        rc = ORCM_ERROR;
        ERR_MSG_RDT(PQresultErrorMessage(res));
        goto cleanup_and_exit;
    }
    PQclear(res);
    res = NULL;

    free(index_str);
    index_str = NULL;

    if (NULL == test_params) {
        /* No test parameters provided, we're done! */
        if (mod->autocommit) {
            res = PQexec(mod->conn, "commit");
            if (!status_ok(res)) {
                rc = ORCM_ERROR;
                ERR_MSG_FMT_RDT("Unable to commit transaction: %s",
                                PQresultErrorMessage(res));
                goto cleanup_and_exit;
            }
            PQclear(res);
            res = NULL;
        }

        opal_output_verbose(2, orcm_db_base_framework.framework_output,
                            "postgres_record_diag_test succeeded");
        goto cleanup_and_exit;
    }

    /* Prepare the statement only if it hasn't already been prepared */
    if (!mod->prepared[ORCM_DB_PG_STMT_RECORD_DIAG_TEST_CONFIG]) {
        /*
         * $1: hostname
         * $2: diagnostic type
         * $3: diagnostic subtype
         * $4: start time
         * $5: test parameter
         * $6: data type
         * $7: integer value
         * $8: real value
         * $9: string value
         * $10: units
         */
        res = PQprepare(mod->conn, "record_diag_test_config",
                        "select record_diag_test_config("
                        "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10)",
                        0, NULL);
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_RDT(PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        mod->prepared[ORCM_DB_PG_STMT_RECORD_DIAG_TEST_CONFIG] = true;
    }

    config_params[0] = hostname;
    config_params[1] = diag_type;
    config_params[2] = diag_subtype;
    config_params[3] = start_time_str;
    OPAL_LIST_FOREACH(mv, test_params, orcm_metric_value_t) {
        if (NULL == mv->value.key || 0 == strlen(mv->value.key)) {
            rc = ORCM_ERR_BAD_PARAM;
            ERR_MSG_RDT("Key or node feature name not provided for value");
            goto cleanup_and_exit;
        }

        ret = opal_value_to_orcm_db_item(&mv->value, &item);
        if (ORCM_SUCCESS != ret) {
            rc = ORCM_ERR_NOT_SUPPORTED;
            ERR_MSG_FMT_RDT("Unsupported data type: %s",
                            opal_dss.lookup_data_type(mv->value.type));
            goto cleanup_and_exit;
        }

        config_params[4] = mv->value.key;
        asprintf(&type_str, "%d", item.opal_type);
        config_params[5] = type_str;
        switch (item.item_type) {
        case ORCM_DB_ITEM_STRING:
            config_params[6] = NULL;
            config_params[7] = NULL;
            config_params[8] = item.value.value_str;
            break;
        case ORCM_DB_ITEM_REAL:
            config_params[6] = NULL;
            asprintf(&value_str, "%f", item.value.value_real);
            config_params[7] = value_str;
            config_params[8] = NULL;
            break;
        default:
            asprintf(&value_str, "%lld", item.value.value_int);
            config_params[6] = value_str;
            config_params[7] = NULL;
            config_params[8] = NULL;
        }
        config_params[9] = mv->units;

        res = PQexecPrepared(mod->conn, "record_diag_test_config",
                             NUM_TEST_CONFIG_PARAMS, config_params, NULL,
                             NULL, 0);
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_RDT(PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        free(type_str);
        type_str = NULL;
        if (NULL != value_str) {
            free(value_str);
            value_str = NULL;
        }
    }

    if (mod->autocommit) {
        res = PQexec(mod->conn, "commit");
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_FMT_RDT("Unable to commit transaction: %s",
                            PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;
    }

    opal_output_verbose(2, orcm_db_base_framework.framework_output,
                        "postgres_record_diag_test succeeded");

cleanup_and_exit:
    /* If we're in auto commit mode, then make sure our local changes
     * are either committed or canceled. */
    if (ORCM_SUCCESS != rc && mod->autocommit && local_tran_started) {
        res = PQexec(mod->conn, "rollback");
    }

    if (NULL != res) {
        PQclear(res);
    }

    if (NULL != index_str) {
        free(index_str);
    }

    if (NULL != type_str) {
        free(type_str);
    }

    if (NULL != value_str) {
        free(value_str);
    }

    return rc;
}

static int postgres_store_diag_test(mca_db_postgres_module_t *mod,
                                    opal_list_t *input,
                                    opal_list_t *ret)
{
    int rc = ORCM_SUCCESS;

    const int NUM_PARAMS = 7;
    const char *params[] = {
        "hostname",
        "diag_type",
        "diag_subtype",
        "start_time",
        "test_result",
        "end_time",
        "component_index"
    };
    opal_value_t *param_items[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    opal_bitmap_t item_bm;
    size_t num_params_found;
    size_t num_items;

    const int NUM_TEST_RESULT_PARAMS = 7;
    const int NUM_TEST_CONFIG_PARAMS = 10;

    const char *result_params[NUM_TEST_RESULT_PARAMS];
    const char *config_params[NUM_TEST_CONFIG_PARAMS];

    char *hostname = NULL;
    char *diag_type = NULL;
    char *diag_subtype = NULL;
    char *test_result = NULL;
    int *component_index = NULL;

    char *index_str = NULL;
    char *type_str = NULL;
    char *value_str = NULL;
    char start_time_str[40];
    char end_time_str[40];

    orcm_metric_value_t *mv;
    opal_value_t *kv;
    orcm_db_item_t item;
    int i;

    PGresult *res = NULL;

    bool local_tran_started = false;

    num_items = opal_list_get_size(input);
    OBJ_CONSTRUCT(&item_bm, opal_bitmap_t);
    opal_bitmap_init(&item_bm, (int)num_items);

    /* Get the main parameters from the list */
    num_params_found = find_items(params, NUM_PARAMS, input, param_items,
                                  &item_bm);

    /* Check the parameters */
    if (NULL == param_items[0]) {
        ERR_MSG_RDT("No hostname provided");
        OBJ_DESTRUCT(&item_bm);
        return ORCM_ERR_BAD_PARAM;
    }

    if (NULL == param_items[1]) {
        ERR_MSG_RDT("No diagnostic type provided");
        OBJ_DESTRUCT(&item_bm);
        return ORCM_ERR_BAD_PARAM;
    }

    if (NULL == param_items[2]) {
        ERR_MSG_RDT("No diagnostic subtype provided");
        OBJ_DESTRUCT(&item_bm);
        return ORCM_ERR_BAD_PARAM;
    }

    if (NULL == param_items[3]) {
        ERR_MSG_RDT("No start time provided");
        OBJ_DESTRUCT(&item_bm);
        return ORCM_ERR_BAD_PARAM;
    }

    if (NULL == param_items[4]) {
        ERR_MSG_RDT("No test result provided");
        OBJ_DESTRUCT(&item_bm);
        return ORCM_ERR_BAD_PARAM;
    }

    kv = param_items[0];
    if (OPAL_STRING == kv->type) {
        hostname = kv->data.string;
    } else {
        ERR_MSG_RDT("Invalid value type specified for hostname");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    kv = param_items[1];
    if (OPAL_STRING == kv->type) {
        diag_type = kv->data.string;
    } else {
        ERR_MSG_RDT("Invalid value type specified for diagnostic "
                      "type");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    kv = param_items[2];
    if (OPAL_STRING == kv->type) {
        diag_subtype = kv->data.string;
    } else {
        ERR_MSG_RDT("Invalid value type specified for diagnostic "
                      "subtype");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    kv = param_items[3];
    switch (kv->type) {
    case OPAL_TIMEVAL:
    case OPAL_TIME:
        if (!tv_to_str_time_stamp(&kv->data.tv, start_time_str,
                                  sizeof(start_time_str))) {
            ERR_MSG_RDT("Failed to convert start time stamp "
                          "value");
            rc = ORCM_ERR_BAD_PARAM;
            goto cleanup_and_exit;
        }
        break;
    case OPAL_STRING:
        strncpy(start_time_str, kv->data.string,
                sizeof(start_time_str) - 1);
        start_time_str[sizeof(start_time_str) - 1] = '\0';
        break;
    default:
        ERR_MSG_RDT("Invalid value type specified for start time");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    kv = param_items[4];
    if (OPAL_STRING == kv->type) {
        test_result = kv->data.string;
    } else {
        ERR_MSG_RDT("Invalid value type specified for test result");
        rc = ORCM_ERR_BAD_PARAM;
        goto cleanup_and_exit;
    }

    if (NULL != (kv = param_items[5])) {
        switch (kv->type) {
        case OPAL_TIMEVAL:
        case OPAL_TIME:
            if (!tv_to_str_time_stamp(&kv->data.tv, end_time_str,
                                      sizeof(end_time_str))) {
                ERR_MSG_RDT("Failed to convert end time stamp value");
                rc = ORCM_ERR_BAD_PARAM;
                goto cleanup_and_exit;
            }
            break;
        case OPAL_STRING:
            strncpy(end_time_str, kv->data.string,
                    sizeof(end_time_str) - 1);
            end_time_str[sizeof(end_time_str) - 1] = '\0';
            break;
        default:
            ERR_MSG_RDT("Invalid value type specified for end time");
            rc = ORCM_ERR_BAD_PARAM;
            goto cleanup_and_exit;
        }
    }

    if (NULL != (kv = param_items[6])) {
        if (OPAL_INT == kv->type) {
            component_index = &kv->data.integer;
        } else {
            ERR_MSG_RDT("Invalid value type specified for component "
                          "index");
            rc = ORCM_ERR_BAD_PARAM;
            goto cleanup_and_exit;
        }
    }

    /* Prepare the statement only if it hasn't already been prepared */
    if (!mod->prepared[ORCM_DB_PG_STMT_RECORD_DIAG_TEST_RESULT]) {
        /*
         * $1: hostname
         * $2: diagnostic type
         * $3: diagnostic subtype
         * $4: start time
         * $5: end time
         * $6: component index
         * $7: test result
         */
        res = PQprepare(mod->conn, "record_diag_test_result",
                        "select record_diag_test_result("
                        "$1, $2, $3, $4, $5, $6, $7)",
                        0, NULL);
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_RDT(PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        mod->prepared[ORCM_DB_PG_STMT_RECORD_DIAG_TEST_RESULT] = true;
    }

    if (mod->autocommit || !mod->tran_started) {
        res = PQexec(mod->conn, "begin");
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_FMT_RDT("Unable to begin transaction: %s",
                            PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        local_tran_started = true;
    }

    if (NULL == component_index) {
        index_str = NULL;
    } else {
        asprintf(&index_str, "%d", *component_index);
    }


    /* Build and execute the SQL command to store the diagnostic test result */
    result_params[0] = hostname;
    result_params[1] = diag_type;
    result_params[2] = diag_subtype;
    result_params[3] = start_time_str;
    if (NULL != param_items[5]) {
        result_params[4] = end_time_str;
    } else {
        result_params[4] = NULL;
    }
    result_params[5] = index_str;
    result_params[6] = test_result;

    res = PQexecPrepared(mod->conn, "record_diag_test_result",
                         NUM_TEST_RESULT_PARAMS, result_params, NULL, NULL, 0);
    if (!status_ok(res)) {
        rc = ORCM_ERROR;
        ERR_MSG_RDT(PQresultErrorMessage(res));
        goto cleanup_and_exit;
    }
    PQclear(res);
    res = NULL;

    free(index_str);
    index_str = NULL;

    if (num_items <= num_params_found) {
        /* No test parameters provided, we're done! */
        if (mod->autocommit) {
            res = PQexec(mod->conn, "commit");
            if (!status_ok(res)) {
                rc = ORCM_ERROR;
                ERR_MSG_FMT_RDT("Unable to commit transaction: %s",
                                PQresultErrorMessage(res));
                goto cleanup_and_exit;
            }
            PQclear(res);
            res = NULL;
        }

        opal_output_verbose(2, orcm_db_base_framework.framework_output,
                            "postgres_record_diag_test succeeded");
        goto cleanup_and_exit;
    }

    /* Prepare the statement only if it hasn't already been prepared */
    if (!mod->prepared[ORCM_DB_PG_STMT_RECORD_DIAG_TEST_CONFIG]) {
        /*
         * $1: hostname
         * $2: diagnostic type
         * $3: diagnostic subtype
         * $4: start time
         * $5: test parameter
         * $6: data type
         * $7: integer value
         * $8: real value
         * $9: string value
         * $10: units
         */
        res = PQprepare(mod->conn, "record_diag_test_config",
                        "select record_diag_test_config("
                        "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10)",
                        0, NULL);
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_RDT(PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        mod->prepared[ORCM_DB_PG_STMT_RECORD_DIAG_TEST_CONFIG] = true;
    }

    /* Build and execute the SQL commands to store the test parameters (if
     * provided in the list) */
    config_params[0] = hostname;
    config_params[1] = diag_type;
    config_params[2] = diag_subtype;
    config_params[3] = start_time_str;
    i = 0;
    OPAL_LIST_FOREACH(mv, input, orcm_metric_value_t) {
        /* Ignore the items that have already been processed */
        if (opal_bitmap_is_set_bit(&item_bm, i)) {
            i++;
            continue;
        }

        if (NULL == mv->value.key || 0 == strlen(mv->value.key)) {
            rc = ORCM_ERR_BAD_PARAM;
            ERR_MSG_RDT("Key or node feature name not provided for value");
            goto cleanup_and_exit;
        }

        if (ORCM_SUCCESS != opal_value_to_orcm_db_item(&mv->value, &item)) {
            rc = ORCM_ERR_NOT_SUPPORTED;
            ERR_MSG_FMT_RDT("Unsupported data type: %s",
                            opal_dss.lookup_data_type(mv->value.type));
            goto cleanup_and_exit;
        }

        config_params[4] = mv->value.key;
        asprintf(&type_str, "%d", item.opal_type);
        config_params[5] = type_str;
        switch (item.item_type) {
        case ORCM_DB_ITEM_STRING:
            config_params[6] = NULL;
            config_params[7] = NULL;
            config_params[8] = item.value.value_str;
            break;
        case ORCM_DB_ITEM_REAL:
            config_params[6] = NULL;
            asprintf(&value_str, "%f", item.value.value_real);
            config_params[7] = value_str;
            config_params[8] = NULL;
            break;
        default:
            asprintf(&value_str, "%lld", item.value.value_int);
            config_params[6] = value_str;
            config_params[7] = NULL;
            config_params[8] = NULL;
        }
        config_params[9] = mv->units;

        res = PQexecPrepared(mod->conn, "record_diag_test_config",
                             NUM_TEST_CONFIG_PARAMS, config_params, NULL,
                             NULL, 0);
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_RDT(PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;

        free(type_str);
        type_str = NULL;
        if (NULL != value_str) {
            free(value_str);
            value_str = NULL;
        }
        i++;
    }

    if (mod->autocommit) {
        res = PQexec(mod->conn, "commit");
        if (!status_ok(res)) {
            rc = ORCM_ERROR;
            ERR_MSG_FMT_RDT("Unable to commit transaction: %s",
                            PQresultErrorMessage(res));
            goto cleanup_and_exit;
        }
        PQclear(res);
        res = NULL;
    }

    opal_output_verbose(2, orcm_db_base_framework.framework_output,
                        "postgres_record_diag_test succeeded");

cleanup_and_exit:
    /* If we're in auto commit mode, then make sure our local changes
     * are either committed or canceled. */
    if (ORCM_SUCCESS != rc && mod->autocommit && local_tran_started) {
        res = PQexec(mod->conn, "rollback");
    }

    if (NULL != res) {
        PQclear(res);
    }

    if (NULL != index_str) {
        free(index_str);
    }

    if (NULL != type_str) {
        free(type_str);
    }

    if (NULL != value_str) {
        free(value_str);
    }

    OBJ_DESTRUCT(&item_bm);
    return rc;
}

#define ERR_MSG_COMMIT(msg) \
    opal_output(0, "***********************************************"); \
    opal_output(0, "db:postgres: Unable to commit current transaction: "); \
    opal_output(0, msg); \
    opal_output(0, "***********************************************");

static int postgres_commit(struct orcm_db_base_module_t *imod)
{
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;
    PGresult *res;

    res = PQexec(mod->conn, "commit");
    if (!status_ok(res)) {
        PQclear(res);
        ERR_MSG_COMMIT(PQresultErrorMessage(res));
        return ORCM_ERROR;
    }
    PQclear(res);

    mod->tran_started = false;

    return ORCM_SUCCESS;
}

#define ERR_MSG_ROLLBACK(msg) \
    opal_output(0, "***********************************************"); \
    opal_output(0, "db:postgres: Unable to cancel current transaction: "); \
    opal_output(0, msg); \
    opal_output(0, "***********************************************");

static int postgres_rollback(struct orcm_db_base_module_t *imod)
{
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;
    PGresult *res;

    res = PQexec(mod->conn, "rollback");
    if (!status_ok(res)) {
        PQclear(res);
        ERR_MSG_ROLLBACK(PQresultErrorMessage(res));
        return ORCM_ERROR;
    }
    PQclear(res);

    mod->tran_started = false;

    return ORCM_SUCCESS;
}

static bool tv_to_str_time_stamp(const struct timeval *time, char *tbuf,
                                 size_t size)
{
    struct timeval nrm_time = *time;
    struct tm *tm_info;
    char date_time[30];
    char fraction[10];

    /* Normalize */
    while (nrm_time.tv_usec < 0) {
        nrm_time.tv_usec += 1000000;
        nrm_time.tv_sec--;
    }
    while (nrm_time.tv_usec >= 1000000) {
        nrm_time.tv_usec -= 1000000;
        nrm_time.tv_sec++;
    }

    /* Print in format: YYYY-MM-DD HH:MM:SS.fraction time zone */
    tm_info = localtime(&nrm_time.tv_sec);
    if (NULL != tm_info) {
        strftime(date_time, sizeof(date_time), "%F %T", tm_info);
        snprintf(fraction, sizeof(fraction), "%f",
                 (float)(time->tv_usec / 1000000.0));
        snprintf(tbuf, size, "%s%s", date_time, fraction + 1);
        return true;
    } else {
        return false;
    }
}

static bool time_stamp_str_to_tv(char* stamp, struct timeval* time_value)
{
    char buffer[5]; // largest component is year 4 + 1 for '\0';
    struct tm t;

    if(NULL == stamp || NULL == time_value) {
        return false;
    }
    memset(&t, 0, sizeof(struct tm));

    strncpy(buffer, stamp, 4); /* get year */
    buffer[4] = '\0';
    t.tm_year = atoi(buffer) - 1900;

    strncpy(buffer, &stamp[5], 2); /* get month */
    buffer[2] = '\0';
    t.tm_mon = atoi(buffer) - 1;

    strncpy(buffer, &stamp[8], 2); /* get day of month */
    buffer[2] = '\0';
    t.tm_mday = atoi(buffer);

    strncpy(buffer, &stamp[11], 2); /* get hour of day */
    buffer[2] = '\0';
    t.tm_hour = atoi(buffer);

    strncpy(buffer, &stamp[14], 2); /* get minute of the hour */
    buffer[2] = '\0';
    t.tm_min = atoi(buffer);

    strncpy(buffer, &stamp[17], 2); /* get seconds of the minute */
    buffer[2] = '\0';
    t.tm_sec = atoi(buffer);

    if(23 <= strlen(stamp)) {
        strncpy(buffer, &stamp[20], 3); /* get microseconds (resolution is milliseconds) of the second */
        buffer[3] = '\0';
        time_value->tv_usec = atoi(buffer) * 1000;
    } else {
        time_value->tv_usec = 0;
    }
    time_value->tv_sec = mktime(&t);

    return true;
}

static void tm_to_str_time_stamp(const struct tm *time, char *tbuf,
                                 size_t size)
{
    strftime(tbuf, size, "%F %T", time);
}

static inline bool status_ok(PGresult *res)
{
    ExecStatusType status = PQresultStatus(res);
    return (status == PGRES_COMMAND_OK
            || status == PGRES_TUPLES_OK
            || status == PGRES_NONFATAL_ERROR);
}

#define ERR_MSG_FMT_FETCH(msg, ...) \
    opal_output(0, "***********************************************"); \
    opal_output(0, "db:postgres: Unable to fetch data: "); \
    opal_output(0, msg, ##__VA_ARGS__); \
    opal_output(0, "***********************************************");

/**************************************************************************************************
 * If fetch features are added or column names change or opal types change for a column name
 * change here!!!
 **************************************************************************************************/
static const char *opal_type_column_name = "data_type_id";
static const char *value_column_names[] = {
    "value_str",
    "value_int",
    "value_real",
    NULL
};

static int get_str_column(PGresult* results)
{
    return PQfnumber(results, value_column_names[0]);
}

static int get_real_column(PGresult* results)
{
    return PQfnumber(results, value_column_names[2]);
}

static int get_int_column(PGresult* results)
{
    return PQfnumber(results, value_column_names[1]);
}

static bool is_column_name_a_value_column(const char* column_name)
{
    int i = 0;
    bool rv = false;

    while(NULL != value_column_names[i]) {
        if(0 == strcmp(column_name, value_column_names[i])) {
            rv = true;
            break;
        }
        ++i;
    }
    return rv;
}

static opal_data_type_t get_opal_type_from_postgres_type(Oid pg_type)
{
    switch(pg_type) {
    case BOOLOID:
        return OPAL_BOOL;
    case BYTEAOID:
        return OPAL_BYTE;
    case CHAROID:
        return OPAL_INT8;
    case NAMEOID:
    case BPCHAROID:
    case VARCHAROID:
    case TSVECTOROID:
    case CSTRINGOID:
        return OPAL_STRING;
    case INT8OID:
        return OPAL_INT64;
    case INT2OID:
        return OPAL_INT16;
    case INT2VECTOROID:
    case INT2ARRAYOID:
        return OPAL_INT16_ARRAY;
    case INT4OID:
        return OPAL_INT32;
    case TEXTOID:
        return OPAL_STRING;
    case OIDOID:
    case XIDOID:
    case CIDOID:
    case REGTYPEOID:
        return OPAL_UINT32;
    case TIDOID:
    case BITOID:
    case VARBITOID:
    case UUIDOID:
        return OPAL_UINT8_ARRAY;
    case OIDVECTOROID:
    case INT4ARRAYOID:
    case OIDARRAYOID:
    case REGTYPEARRAYOID:
        return OPAL_UINT32_ARRAY;
    case FLOAT4OID:
        return OPAL_FLOAT;
    case FLOAT8OID:
        return OPAL_DOUBLE;
    case ABSTIMEOID:
    case TIMESTAMPOID:
    case TIMESTAMPTZOID:
    case DATEOID:
        return OPAL_TIMEVAL;
    case TEXTARRAYOID:
    case CSTRINGARRAYOID:
        return OPAL_STRING_ARRAY;
    case FLOAT4ARRAYOID:
        return OPAL_FLOAT_ARRAY;
    case TIMETZOID:
    case INTERVALOID:
    case TIMEOID:
        return OPAL_TIME;
    default:
        return OPAL_UNDEF;
    }
}

/**************************************************************************************************/

#define NO_ROWS     (-1)
#define NO_COLUMN   NO_ROWS

static int postgres_fetch(struct orcm_db_base_module_t *imod,
                         const char *view,
                         opal_list_t *filters,
                         opal_list_t *kvs)
{
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;
    int rc = ORCM_SUCCESS;
    PGresult *results = NULL;
    int handle = -1;
    opal_value_t *result = NULL;
    char* query = NULL;

    if(NULL == view || 0 == strlen(view)) {
        ERR_MSG_FMT_FETCH("database view passed was %s or empty!", "NULL");
        return ORCM_ERR_NOT_IMPLEMENTED;
    }

    if(NULL == kvs) {
        ERR_MSG_FMT_FETCH("Argument 'kvs' passed was NULL but was expected to be valid opal_list_t pointer: %d", 0);
        rc = ORCM_ERROR;
        goto cleanup_and_exit;
    }
    query = build_query_from_view_name_and_filters(view, filters);
    if(NULL == query) {
        ERR_MSG_FMT_FETCH("build_query_from_view_name_and_filters returned: %s", "NULL");
        rc = ORCM_ERROR;
        goto cleanup_and_exit;
    }
    results = PQexec(mod->conn, query);
    if(!status_ok(results)) {
        ERR_MSG_FMT_FETCH("PQexec returned: %s", "NULL");
        rc = ORCM_ERROR;
        goto cleanup_and_exit;
    }

    /* Add new results set handle */
    handle = opal_pointer_array_add(mod->results_sets, (void*)results);
    if(-1 == handle) {
        rc = ORCM_ERROR;
        ERR_MSG_FMT_FETCH("opal_pointer_array_add returned: %d", -1);
        goto cleanup_and_exit;
    }
    mod->current_row = 0; /* Simulate 'get_next_row' functionality. */

    /* Create returned handle object */
    result = OBJ_NEW(opal_value_t);
    result->type = OPAL_INT;
    result->data.integer = handle;
    opal_list_append(kvs, (void*)result); /* takes ownership */

cleanup_and_exit:
    free(query);
    return rc;
}

static int postgres_get_num_rows(struct orcm_db_base_module_t *imod,
                                 int rshandle,
                                 int *num_rows)
{
    int rc = ORCM_ERROR;
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;
    PGresult *results = (PGresult*)opal_pointer_array_get_item(mod->results_sets, rshandle);
    if(NULL != results) {
        int row_count = PQntuples(results);
        if(NO_ROWS <= row_count) { /* No rows (0) is OK */
            *num_rows = row_count;
            rc = ORCM_SUCCESS;
        }
    }
    return rc;
}

static bool is_supported_opal_int_type(opal_data_type_t type)
{
    if(OPAL_BYTE == type || OPAL_BOOL == type || OPAL_SIZE == type || OPAL_PID == type ||
       OPAL_INT == type || OPAL_INT8 == type || OPAL_INT16 == type || OPAL_INT32 == type || OPAL_INT64 == type ||
       OPAL_UINT == type || OPAL_UINT8 == type || OPAL_UINT16 == type || OPAL_UINT32 == type || OPAL_UINT64 == type) {
        return true;
    } else {
        return false;
    }
}

static opal_value_t *get_value_object(PGresult *results, int row, opal_data_type_t type)
{
    if(OPAL_STRING == type && NO_COLUMN == get_str_column(results)) {
        return NULL;
    }
    if((OPAL_FLOAT == type || OPAL_DOUBLE == type) && NO_COLUMN == get_real_column(results)) {
        return NULL;
    }
    if(true == is_supported_opal_int_type(type) && NO_COLUMN == get_real_column(results)) {
        return NULL;
    }
    opal_value_t *rv = OBJ_NEW(opal_value_t);
    rv->type = type;
    rv->key = strdup("value");
    switch(type) { /* all PQgetvalue buffers are owned by PQ */
        case OPAL_BYTE:
            rv->data.byte = *((uint8_t*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_BOOL:
            rv->data.flag = ((*((uint8_t*)PQgetvalue(results, row, get_int_column(results)))) == 0)?false:true;
            break;
        case OPAL_STRING:
            rv->data.string = strdup(PQgetvalue(results, row, get_str_column(results)));
            break;
        case OPAL_SIZE:
            rv->data.size = *((size_t*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_PID:
            rv->data.pid = *((pid_t*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_INT:
            rv->data.integer = *((int*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_INT8:
            rv->data.int8 = *((int8_t*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_INT16:
            rv->data.int16 = *((int16_t*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_INT32:
            rv->data.int32 = *((int32_t*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_INT64:
            rv->data.int64 = *((int64_t*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_UINT:
            rv->data.uint = *((unsigned int*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_UINT8:
            rv->data.uint8 = *((uint8_t*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_UINT16:
            rv->data.uint16 = *((uint16_t*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_UINT32:
            rv->data.uint32 = *((uint32_t*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_UINT64:
            rv->data.uint64 = *((uint64_t*)PQgetvalue(results, row, get_int_column(results)));
            break;
        case OPAL_FLOAT:
            rv->data.fval = (float)*((double*)PQgetvalue(results, row, get_real_column(results)));
            break;
        case OPAL_DOUBLE:
            rv->data.dval = *((double*)PQgetvalue(results, row, get_real_column(results)));
            break;
        default: /* No supported basic data type so ignore it */
            ERR_MSG_FMT_FETCH("An unsupported orcm value type was encountered: %d", type);
            OBJ_RELEASE(rv);
            return NULL;
    }

    return rv;
}

static int postgres_get_next_row(struct orcm_db_base_module_t *imod,
                                 int rshandle,
                                 opal_list_t *row)
{
    int rc = ORCM_SUCCESS;
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;
    PGresult *results = (PGresult*)opal_pointer_array_get_item(mod->results_sets, rshandle);
    int cols = -1;
    int data_type_col = -1;
    opal_data_type_t data_type = OPAL_UNDEF;
    int data_length = -1;
    opal_value_t *value_object = NULL;
    bool inserted_value = false;

    if(NULL == results) {
        rc = ORCM_ERROR;
        ERR_MSG_FMT_FETCH("Bad results handle: %d", rshandle);
        goto next_cleanup;
    }

    /* get row ORCM data value type */
    data_type_col = PQfnumber(results, opal_type_column_name);
    if(NO_COLUMN != data_type_col) {
        data_type = atoi(PQgetvalue(results, mod->current_row, data_type_col));

        /* retrieve 1 correctly typed object for one of 'value_str', 'value_int', 'value_real' */
        value_object = get_value_object(results, mod->current_row, data_type);
    } else {
        data_type_col = PQfnumber(results, "value_int");
        data_length = PQgetlength(results, mod->current_row, data_type_col);
        if(0 >= data_length) {
            data_type_col = PQfnumber(results, "value_real");
            data_length = PQgetlength(results, mod->current_row, data_type_col);
            if(0 >= data_length) {
                data_type_col = PQfnumber(results, "value_str");
                data_length = PQgetlength(results, mod->current_row, data_type_col);
                if(0 >= data_length) {
                    data_type = OPAL_UNDEF;
                } else {
                    data_type = OPAL_STRING;
                }
            } else {
                data_type = OPAL_DOUBLE;
            }
        } else {
            data_type = OPAL_INT64;
        }
        /* retrieve 1 correctly typed object for one of 'value_str', 'value_int', 'value_real' */
        value_object = get_value_object(results, mod->current_row, data_type);
        data_type_col = -1;
    }

    /* Get row general column data */
    cols = PQnfields(results);
    for(int i = 0; i < cols; ++i) {
        Oid pg_type;
        opal_value_t* kv = NULL;
        opal_data_type_t type = OPAL_UNDEF;
        char* name_ref = NULL;
        if(i == data_type_col) { /* skip data_type_id column */
            continue;
        }

        name_ref = PQfname(results, i);
        pg_type = PQftype(results, i);
        if(true == is_column_name_a_value_column(name_ref)) {
            if(false == inserted_value && NULL != value_object) {
                opal_list_append(row, &value_object->super);
                value_object = NULL;
                inserted_value = true;
            }
            continue; /* skip all 'value_*' named columns as thjey are already handled */
        }

        type = get_opal_type_from_postgres_type(pg_type);
        if(OPAL_UNDEF == type) {
            ERR_MSG_FMT_FETCH("An unrecognized column name '%s' was encountered and skipped during a call to 'get_next_row'", name_ref);
            continue; /* skip unknown fields */
        }
        kv = OBJ_NEW(opal_value_t);
        kv->key = strdup(name_ref);
        kv->type = type;
        switch(type) {
            case OPAL_STRING:
                kv->data.string = strdup(PQgetvalue(results, mod->current_row, i));
                break;
            case OPAL_INT:
                kv->data.integer = atoi(PQgetvalue(results, mod->current_row, i));
                break;
            case OPAL_TIMEVAL:
                time_stamp_str_to_tv(PQgetvalue(results, mod->current_row, i), &kv->data.tv);
                break;
        }
        opal_list_append(row, &kv->super);
    }
    ++(mod->current_row); /* move to next row */

next_cleanup:
    free(value_object);
    return rc;
}

static int postgres_close_result_set(struct orcm_db_base_module_t *imod,
                                     int rshandle)
{
    mca_db_postgres_module_t *mod = (mca_db_postgres_module_t*)imod;
    PGresult *results = (PGresult*)opal_pointer_array_get_item(mod->results_sets, rshandle);

    if(NULL == results) {
        ERR_MSG_FMT_FETCH("Bad results handle: %d", rshandle);
        PQclear(results);
        return ORCM_ERROR;
    }

    PQclear(results);

    opal_pointer_array_set_item(mod->results_sets, rshandle, NULL);

    return ORCM_SUCCESS;
}
