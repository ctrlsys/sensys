/*
 * Copyright (c) 2012-2013 Los Alamos National Security, Inc. All rights reserved.
 * Copyright (c) 2014-2015 Intel, Inc. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 */

#include "orcm_config.h"
#include "orcm/constants.h"

#include <time.h>
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

#include "opal/class/opal_pointer_array.h"
#include "opal/util/argv.h"
#include "opal/util/output.h"
#include "opal_stdint.h"

#include "orcm/runtime/orcm_globals.h"

#include "orcm/mca/db/base/base.h"
#include "db_print.h"

#define DB_PRINT_BUF_SIZE 1024

/* Module API functions */
static int init(struct orcm_db_base_module_t *imod);
static void finalize(struct orcm_db_base_module_t *imod);
static int store(struct orcm_db_base_module_t *imod,
                 const char *primary_key,
                 opal_list_t *kvs);
static int store_new(struct orcm_db_base_module_t *imod,
                     orcm_db_data_type_t data_type,
                     opal_list_t *kvs,
                     opal_list_t *ret);
static int record_data_samples(struct orcm_db_base_module_t *imod,
                               const char *hostname,
                               const struct timeval *time_stamp,
                               const char *data_group,
                               opal_list_t *samples);
static int update_node_features(struct orcm_db_base_module_t *imod,
                                const char *hostname,
                                opal_list_t *features);
static int record_diag_test(struct orcm_db_base_module_t *imod,
                            const char *hostname,
                            const char *diag_type,
                            const char *diag_subtype,
                            const struct tm *start_time,
                            const struct tm *end_time,
                            const int *component_index,
                            const char *test_result,
                            opal_list_t *test_params);

/* Internal helper functions */
static void print_values(opal_list_t *values, char ***cmdargs);
static void print_value(const opal_value_t *kv, char *tbuf, size_t size);
static void print_time_value(const struct timeval *time, char *tbuf, size_t size);
static void print_decode_opal_list(opal_list_t *kvs, char ***cmdargs);
static void print_opal_value_t(opal_list_item_t *list_item, char *tbuf);
static void print_orcm_metric_t(opal_list_item_t *list_item, char *tbuf);

db_print_types_t types[] = {
    {"opal_value_t", print_opal_value_t},
    {"orcm_metric_value_t", print_orcm_metric_t},
};


mca_db_print_module_t mca_db_print_module = {
    {
        init,
        finalize,
        store,
        store_new,
        record_data_samples,
        update_node_features,
        record_diag_test,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    },
};

static int init(struct orcm_db_base_module_t *imod)
{
    mca_db_print_module_t *mod = (mca_db_print_module_t*)imod;

    if (0 == strcmp(mod->file, "-")) {
        mod->fp = stdout;
    } else if (0 == strcmp(mod->file, "+")) {
        mod->fp = stderr;
    } else if (NULL == (mod->fp = fopen(mod->file, "w"))) {
        opal_output(0, "ERROR: cannot open log file %s", mod->file);
        return ORCM_ERROR;
    }

    return ORCM_SUCCESS;
}

static void finalize(struct orcm_db_base_module_t *imod)
{
    mca_db_print_module_t *mod = (mca_db_print_module_t*)imod;

    if (NULL != mod->fp &&
        stdout != mod->fp &&
        stderr != mod->fp) {
        fclose(mod->fp);
    }

    free(mod);
}

static int store(struct orcm_db_base_module_t *imod,
                 const char *primary_key,
                 opal_list_t *kvs)
{
    mca_db_print_module_t *mod = (mca_db_print_module_t*)imod;
    char **cmdargs=NULL, *vstr;
    char tbuf[1024];
    opal_value_t *kv;
    char **key_argv;
    int argv_count;
    int len;

    if (NULL != primary_key) {
        snprintf(tbuf, sizeof(tbuf), "%s=%s", "primary_key", primary_key);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    /* cycle through the provided values and print them */
    /* print the data in the following format: <key>=<value>:<units> */
    OPAL_LIST_FOREACH(kv, kvs, opal_value_t) {
        /* the key could include the units (<key>:<units>) */
        key_argv = opal_argv_split(kv->key, ':');
        argv_count = opal_argv_count(key_argv);
        if (0 != argv_count) {
            snprintf(tbuf, sizeof(tbuf), "%s=", key_argv[0]);
            len = strlen(tbuf);
        } else {
            /* no key :o */
            len = 0;
        }

        print_value(kv, tbuf + len, sizeof(tbuf) - len);

        if (argv_count > 1) {
            /* units were included, so add them to the buffer */
            len = strlen(tbuf);
            snprintf(tbuf + len, sizeof(tbuf) - len, ":%s", key_argv[1]);
        }

        opal_argv_append_nosize(&cmdargs, tbuf);

        opal_argv_free(key_argv);
    }

    /* assemble the value string */
    vstr = opal_argv_join(cmdargs, ',');

    /* print it */
    fprintf(mod->fp, "DB request: store; data:\n%s\n", vstr);
    free(vstr);
    opal_argv_free(cmdargs);

    return ORCM_SUCCESS;
}

static int store_new(struct orcm_db_base_module_t *imod,
                      orcm_db_data_type_t data_type,
                      opal_list_t *kvs,
                      opal_list_t *ret)
{
    mca_db_print_module_t *mod = (mca_db_print_module_t*)imod;
    char **cmdargs=NULL, *vstr;


    /* cycle through the provided values and print them */
    /* print the data in the following format: <key>=<value>:<units> */
    print_decode_opal_list(kvs, &cmdargs);

    vstr = opal_argv_join(cmdargs, ',');

    fprintf(mod->fp, "DB request: store; data:\n%s\n", vstr);
    free(vstr);
    opal_argv_free(cmdargs);

    return ORCM_SUCCESS;

}

static void print_decode_opal_list(opal_list_t *kvs, char ***cmdargs)
{
    opal_list_item_t *list_item = NULL;
    unsigned int types_index;
    char tbuf[DB_PRINT_BUF_SIZE];

    OPAL_LIST_FOREACH(list_item, kvs, opal_list_item_t) {
        for (types_index = 0; types_index < sizeof(types)/sizeof(db_print_types_t); types_index ++) {
            if (0 == strcmp(list_item->super.obj_class->cls_name, types[types_index].name)) {
                types[types_index].print(list_item, tbuf);
                opal_argv_append_nosize(cmdargs, tbuf);
            }
        }
    }
}

static void print_opal_value_t(opal_list_item_t *list_item, char *tbuf)
{
    opal_value_t *kv;
    int len;

    kv =  (opal_value_t *)list_item;
    snprintf(tbuf, DB_PRINT_BUF_SIZE, "%s=", kv->key);
    len = strlen(tbuf);

    print_value(kv, tbuf + len, DB_PRINT_BUF_SIZE - len);
}

static void print_orcm_metric_t(opal_list_item_t *list_item, char *tbuf)
{
    orcm_metric_value_t *kv;
    int len;

    kv =  (orcm_metric_value_t *)list_item;
    snprintf(tbuf, DB_PRINT_BUF_SIZE, "%s=", kv->value.key);
    len = strlen(tbuf);

    print_value(&(kv->value), tbuf + len, DB_PRINT_BUF_SIZE - len);

    if (NULL != kv->units) {
        len = strlen(tbuf);
        snprintf(tbuf + len, DB_PRINT_BUF_SIZE - len, ":%s", kv->units);
    }
}

static int record_data_samples(struct orcm_db_base_module_t *imod,
                              const char *hostname,
                              const struct timeval *time_stamp,
                              const char *data_group,
                              opal_list_t *samples)
{
    mca_db_print_module_t *mod = (mca_db_print_module_t*)imod;

    char **cmdargs=NULL, *vstr;
    char time_str[40];
    char tbuf[1024];

    if (NULL != hostname) {
        snprintf(tbuf, sizeof(tbuf), "%s=%s", "hostname", hostname);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    if (NULL != time_stamp) {
        print_time_value(time_stamp, time_str, sizeof(time_str));
        snprintf(tbuf, sizeof(tbuf), "%s=%s", "time_stamp", time_str);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    if (NULL != data_group) {
        snprintf(tbuf, sizeof(tbuf), "%s=%s", "data_group", data_group);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    if (NULL != samples) {
        print_values(samples, &cmdargs);
    }

    /* assemble the value string */
    vstr = opal_argv_join(cmdargs, ',');

    /* print it */
    fprintf(mod->fp, "DB request: record_data_samples; data:\n%s\n", vstr);
    free(vstr);
    opal_argv_free(cmdargs);

    return ORCM_SUCCESS;
}

static int update_node_features(struct orcm_db_base_module_t *imod,
                                const char *hostname,
                                opal_list_t *features)
{
    mca_db_print_module_t *mod = (mca_db_print_module_t*)imod;

    char **cmdargs=NULL, *vstr;
    char tbuf[1024];

    if (NULL != hostname) {
        snprintf(tbuf, sizeof(tbuf), "%s=%s", "hostname", hostname);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    if (NULL != features) {
        print_values(features, &cmdargs);
    }

    /* assemble the value string */
    vstr = opal_argv_join(cmdargs, ',');

    /* print it */
    fprintf(mod->fp, "DB request: update_node_features; data:\n%s\n", vstr);
    free(vstr);
    opal_argv_free(cmdargs);

    return ORCM_SUCCESS;
}

static int record_diag_test(struct orcm_db_base_module_t *imod,
                            const char *hostname,
                            const char *diag_type,
                            const char *diag_subtype,
                            const struct tm *start_time,
                            const struct tm *end_time,
                            const int *component_index,
                            const char *test_result,
                            opal_list_t *test_params)
{
    mca_db_print_module_t *mod = (mca_db_print_module_t*)imod;

    char **cmdargs=NULL, *vstr;
    char time_str[40];
    char tbuf[1024];

    if (NULL != hostname) {
        snprintf(tbuf, sizeof(tbuf), "%s=%s", "hostname", hostname);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    if (NULL != diag_type) {
        snprintf(tbuf, sizeof(tbuf), "%s=%s", "diag_type", diag_type);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    if (NULL != diag_subtype) {
        snprintf(tbuf, sizeof(tbuf), "%s=%s", "diag_subtype", diag_subtype);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    if (NULL != start_time) {
        strftime(time_str, sizeof(time_str), "%F %T%z", start_time);
        snprintf(tbuf, sizeof(tbuf), "%s=%s", "start_time", time_str);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    if (NULL != end_time) {
        strftime(time_str, sizeof(time_str), "%F %T%z", end_time);
        snprintf(tbuf, sizeof(tbuf), "%s=%s", "end_time", time_str);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    if (NULL != component_index) {
        snprintf(tbuf, sizeof(tbuf), "%s=%d", "component_index", *component_index);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    if (NULL != test_result) {
        snprintf(tbuf, sizeof(tbuf), "%s=%s", "test_result", test_result);
        opal_argv_append_nosize(&cmdargs, tbuf);
    }

    if (NULL != test_params) {
        print_values(test_params, &cmdargs);
    }

    /* assemble the value string */
    vstr = opal_argv_join(cmdargs, ',');

    /* print it */
    fprintf(mod->fp, "DB request: record_diag_test; data:\n%s\n", vstr);
    free(vstr);
    opal_argv_free(cmdargs);

    return ORCM_SUCCESS;
}

static void print_values(opal_list_t *values, char ***cmdargs)
{
    orcm_metric_value_t *mv;
    char tbuf[1024];
    int len;

    /* cycle through the provided values and print them */
    /* print the data in the following format: <key>=<value>:<units> */
    OPAL_LIST_FOREACH(mv, values, orcm_metric_value_t) {
        if (NULL != mv->value.key) {
            snprintf(tbuf, sizeof(tbuf), "%s=", mv->value.key);
            len = strlen(tbuf);
        } else {
            /* no key :o */
            len = 0;
        }

        print_value(&mv->value, tbuf + len, sizeof(tbuf) - len);

        if (NULL != mv->units) {
            /* units were included, so add them to the buffer */
            len = strlen(tbuf);
            snprintf(tbuf + len, sizeof(tbuf) - len, ":%s", mv->units);
        }

        opal_argv_append_nosize(cmdargs, tbuf);
    }
}

static void print_value(const opal_value_t *kv, char *tbuf, size_t size)
{
    switch (kv->type) {
    case OPAL_STRING:
        snprintf(tbuf, size, "%s", kv->data.string);
        break;
    case OPAL_SIZE:
        snprintf(tbuf, size, "%" PRIsize_t "", kv->data.size);
        break;
    case OPAL_INT:
        snprintf(tbuf, size, "%d", kv->data.integer);
        break;
    case OPAL_INT8:
        snprintf(tbuf, size, "%" PRIi8 "", kv->data.int8);
        break;
    case OPAL_INT16:
        snprintf(tbuf, size, "%" PRIi16 "", kv->data.int16);
        break;
    case OPAL_INT32:
        snprintf(tbuf, size, "%" PRIi32 "", kv->data.int32);
        break;
    case OPAL_INT64:
        snprintf(tbuf, size, "%" PRIi64 "", kv->data.int64);
        break;
    case OPAL_UINT:
        snprintf(tbuf, size, "%u", kv->data.uint);
        break;
    case OPAL_UINT8:
        snprintf(tbuf, size, "%" PRIu8 "", kv->data.uint8);
        break;
    case OPAL_UINT16:
        snprintf(tbuf, size, "%" PRIu16 "", kv->data.uint16);
        break;
    case OPAL_UINT32:
        snprintf(tbuf, size, "%" PRIu32 "", kv->data.uint32);
        break;
    case OPAL_UINT64:
        snprintf(tbuf, size, "%" PRIu64 "", kv->data.uint64);
        break;
    case OPAL_PID:
        snprintf(tbuf, size, "%lu", (unsigned long)kv->data.pid);
        break;
    case OPAL_BOOL:
        snprintf(tbuf, size, "%s", kv->data.flag ? "true" : "false");
        break;
    case OPAL_FLOAT:
        snprintf(tbuf, size, "%f", kv->data.fval);
        break;
    case OPAL_DOUBLE:
        snprintf(tbuf, size, "%f", kv->data.dval);
        break;
    case OPAL_TIMEVAL:
    case OPAL_TIME:
        print_time_value(&kv->data.tv, tbuf, size);
        break;
    default:
        snprintf(tbuf, size, "Unsupported type: %s",
                 opal_dss.lookup_data_type(kv->type));
        break;
    }
}

static void print_time_value(const struct timeval *time, char *tbuf, size_t size)
{
    struct timeval nrm_time = *time;
    struct tm *tm_info;
    char date_time[30];
    char fraction[10];
    char time_zone[10];

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
        strftime(time_zone, sizeof(time_zone), "%z", tm_info);
        snprintf(fraction, sizeof(fraction), "%.3f",
                 (float)(time->tv_usec / 1000000.0));
        snprintf(tbuf, size, "%s%s%s", date_time, fraction + 1, time_zone);
    } else {
        snprintf(tbuf, size, "ERROR:  Unable to print time value");
    }

}
