/*
 * Copyright (c) 2009-2011 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2011-2012 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2014      Intel, Inc.  All rights reserved.
 *
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orcm_config.h"
#include "orcm/constants.h"
#include "orcm/types.h"

#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif  /* HAVE_UNISTD_H */
#ifdef HAVE_STRING_H
#include <string.h>
#endif  /* HAVE_STRING_H */
#include <stdio.h>

#include "opal_stdint.h"
#include "opal/class/opal_pointer_array.h"
#include "opal/class/opal_ring_buffer.h"
#include "opal/dss/dss.h"
#include "opal/util/output.h"
#include "opal/mca/pstat/pstat.h"

#include "orte/util/proc_info.h"
#include "orte/util/name_fns.h"
#include "orte/mca/errmgr/errmgr.h"
#include "orte/mca/odls/odls_types.h"
#include "orte/mca/odls/base/odls_private.h"
#include "orte/mca/rml/rml.h"
#include "orte/mca/state/state.h"
#include "orte/runtime/orte_globals.h"
#include "orte/orted/orted.h"

#include "orcm/mca/db/db.h"
#include "orcm/runtime/orcm_globals.h"

#include "orcm/mca/sensor/base/base.h"
#include "orcm/mca/sensor/base/sensor_private.h"
#include "sensor_resusage.h"

/* declare the API functions */
static int init(void);
static void finalize(void);
static void sample(orcm_sensor_sampler_t *sampler);
static void res_log(opal_buffer_t *sample);

/* instantiate the module */
orcm_sensor_base_module_t orcm_sensor_resusage_module = {
    init,
    finalize,
    NULL,
    NULL,
    sample,
    res_log,
    NULL,
    NULL
};

static bool log_enabled = true;
static orte_node_t *my_node;
static orte_proc_t *my_proc;

static void generate_test_vector(opal_buffer_t *v);

static int init(void)
{
    orte_job_t *jdata;

    /* ensure my_proc and my_node are available on the global arrays */
    if (NULL == (jdata = orte_get_job_data_object(ORTE_PROC_MY_NAME->jobid))) {
        my_proc = OBJ_NEW(orte_proc_t);
        my_node = OBJ_NEW(orte_node_t);
    } else {
        if (NULL == (my_proc = (orte_proc_t*)opal_pointer_array_get_item(jdata->procs, ORTE_PROC_MY_NAME->vpid))) {
            ORTE_ERROR_LOG(ORCM_ERR_NOT_FOUND);
            return ORCM_ERR_NOT_FOUND;
        }
        if (NULL == (my_node = my_proc->node)) {
            ORTE_ERROR_LOG(ORCM_ERR_NOT_FOUND);
            return ORCM_ERR_NOT_FOUND;
        }
        /* protect the objects */
        OBJ_RETAIN(my_proc);
        OBJ_RETAIN(my_node);
    }

    return ORCM_SUCCESS;
}

static void finalize(void)
{
    if (NULL != my_proc) {
        OBJ_RELEASE(my_proc);
    }
    if (NULL != my_node) {
        OBJ_RELEASE(my_node);
    }
    return;
}

static void sample(orcm_sensor_sampler_t *sampler)
{
    opal_pstats_t *stats;
    opal_node_stats_t *nstats;
    int rc, i;
    orte_proc_t *child;
    opal_buffer_t buf, *bptr;
    char *comp;
    struct timeval current_time;

    OPAL_OUTPUT_VERBOSE((1, orcm_sensor_base_framework.framework_output,
                         "sample:resusage sampling resource usage"));
    if (mca_sensor_resusage_component.test) {
        /* generate and send a the test vector */
        OBJ_CONSTRUCT(&buf, opal_buffer_t);
        generate_test_vector(&buf);
        bptr = &buf;
        opal_dss.pack(&sampler->bucket, &bptr, 1, OPAL_BUFFER);
        OBJ_DESTRUCT(&buf);
        return;
    }


    /* setup a buffer for our stats */
    OBJ_CONSTRUCT(&buf, opal_buffer_t);
    /* pack our name */
    comp = strdup("resusage");
    if (OPAL_SUCCESS != (rc = opal_dss.pack(&buf, &comp, 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        OBJ_DESTRUCT(&buf);
        return;
    }
    free(comp);

    /* update stats on ourself and the node */
    stats = OBJ_NEW(opal_pstats_t);
    nstats = OBJ_NEW(opal_node_stats_t);
    if (ORCM_SUCCESS != (rc = opal_pstat.query(orte_process_info.pid, stats, nstats))) {
        ORTE_ERROR_LOG(rc);
        OBJ_DESTRUCT(stats);
        OBJ_RELEASE(nstats);
        OBJ_DESTRUCT(&buf);
        return;
    }

    /* the stats framework can't know nodename or rank */
    strncpy(stats->node, orte_process_info.nodename, (OPAL_PSTAT_MAX_STRING_LEN - 1));
    stats->rank = ORTE_PROC_MY_NAME->vpid;
#if 0
    /* locally save the stats */
    if (NULL != (st = (opal_pstats_t*)opal_ring_buffer_push(&my_proc->stats, stats))) {
        OBJ_RELEASE(st);
    }
    if (NULL != (nst = (opal_node_stats_t*)opal_ring_buffer_push(&my_node->stats, nstats))) {
        /* release the popped value */
        OBJ_RELEASE(nst);
    }
#endif

    /* pack them */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(&buf, &orte_process_info.nodename, 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        OBJ_DESTRUCT(&buf);
        return;
    }

    /* get the sample time */
    gettimeofday(&current_time, NULL);

    if (OPAL_SUCCESS != (rc = opal_dss.pack(&buf, &current_time, 1, OPAL_TIMEVAL))) {
        ORTE_ERROR_LOG(rc);
        OBJ_DESTRUCT(&buf);
        return;
    }

    if (OPAL_SUCCESS != (rc = opal_dss.pack(&buf, &nstats, 1, OPAL_NODE_STAT))) {
        ORTE_ERROR_LOG(rc);
        OBJ_DESTRUCT(&buf);
        return;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(&buf, &stats, 1, OPAL_PSTAT))) {
        ORTE_ERROR_LOG(rc);
        OBJ_DESTRUCT(&buf);
        return;
    }

    /* loop through our children and update their stats */
    if (NULL != orte_local_children) {
        for (i=0; i < orte_local_children->size; i++) {
            if (NULL == (child = (orte_proc_t*)opal_pointer_array_get_item(orte_local_children, i))) {
                continue;
            }
            if (!ORTE_FLAG_TEST(child, ORTE_PROC_FLAG_ALIVE)) {
                continue;
            }
            if (0 == child->pid) {
                /* race condition */
                continue;
            }
            stats = OBJ_NEW(opal_pstats_t);
            if (ORCM_SUCCESS != opal_pstat.query(child->pid, stats, NULL)) {
                /* may hit a race condition where the process has
                 * terminated, so just ignore any error
                 */
                OBJ_RELEASE(stats);
                continue;
            }
            /* the stats framework can't know nodename or rank */
            strncpy(stats->node, orte_process_info.nodename, (OPAL_PSTAT_MAX_STRING_LEN - 1));
            stats->rank = child->name.vpid;
#if 0
            /* store it */
            if (NULL != (st = (opal_pstats_t*)opal_ring_buffer_push(&child->stats, stats))) {
                OBJ_RELEASE(st);
            }
#endif
            /* pack them */
            if (OPAL_SUCCESS != (rc = opal_dss.pack(&buf, &stats, 1, OPAL_PSTAT))) {
                ORTE_ERROR_LOG(rc);
                OBJ_DESTRUCT(&buf);
                return;
            }
        }
    }

    /* xfer any data for transmission */
    if (0 < buf.bytes_used) {
        bptr = &buf;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(&sampler->bucket, &bptr, 1, OPAL_BUFFER))) {
            ORTE_ERROR_LOG(rc);
            OBJ_DESTRUCT(&buf);
            return;
        }
    }

#if 0
    /* are there any issues with node-level usage? */
    nst = (opal_node_stats_t*)opal_ring_buffer_poke(&my_node->stats, -1);
    if (NULL != nst && 0.0 < mca_sensor_resusage_component.node_memory_limit) {
        OPAL_OUTPUT_VERBOSE((2, orcm_sensor_base_framework.framework_output,
                             "%s CHECKING NODE MEM",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        /* compute the percentage of node memory in-use */
        in_use = 1.0 - (nst->free_mem / nst->total_mem);
        OPAL_OUTPUT_VERBOSE((2, orcm_sensor_base_framework.framework_output,
                             "%s PERCENT USED: %f LIMIT: %f",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                             in_use, mca_sensor_resusage_component.node_memory_limit));
        if (mca_sensor_resusage_component.node_memory_limit <= in_use) {
            /* loop through our children and find the biggest hog */
            hog = NULL;
            max_mem = 0.0;
            for (i=0; i < orte_local_children->size; i++) {
                if (NULL == (child = (orte_proc_t*)opal_pointer_array_get_item(orte_local_children, i))) {
                    continue;
                }
                if (!ORTE_FLAG_TEST(child, ORTE_PROC_IS_ALIVE)) {
                    continue;
                }
                if (0 == child->pid) {
                    /* race condition */
                    continue;
                }
                if (NULL == (st = (opal_pstats_t*)opal_ring_buffer_poke(&child->stats, -1))) {
                    continue;
                }
                OPAL_OUTPUT_VERBOSE((5, orcm_sensor_base_framework.framework_output,
                                     "%s PROC %s AT VSIZE %f",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(&child->name), st->vsize));
                if (max_mem < st->vsize) {
                    hog = child;
                    max_mem = st->vsize;
                }
            }
            if (NULL == hog) {
                /* if all children dead and we are still too big,
                 * then we must be the culprit - abort
                 */
                OPAL_OUTPUT_VERBOSE((2, orcm_sensor_base_framework.framework_output,
                                     "%s NO CHILD: COMMITTING SUICIDE",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
                orte_errmgr.abort(ORCM_ERR_MEM_LIMIT_EXCEEDED, NULL);
            } else {
                /* report the problem */
                OPAL_OUTPUT_VERBOSE((2, orcm_sensor_base_framework.framework_output,
                                     "%s REPORTING %s TO ERRMGR FOR EXCEEDING LIMITS",
                                     ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                     ORTE_NAME_PRINT(&hog->name)));
                ORTE_ACTIVATE_PROC_STATE(&hog->name, ORTE_PROC_STATE_SENSOR_BOUND_EXCEEDED);
            }
            /* since we have ordered someone to die, we've done enough for this
             * time around - don't check proc limits as well
             */
            return;
        }
    }

    /* check proc limits */
    if (0.0 < mca_sensor_resusage_component.proc_memory_limit) {
        OPAL_OUTPUT_VERBOSE((2, orcm_sensor_base_framework.framework_output,
                             "%s CHECKING PROC MEM",
                             ORTE_NAME_PRINT(ORTE_PROC_MY_NAME)));
        /* check my children first */
        for (i=0; i < orte_local_children->size; i++) {
            if (NULL == (child = (orte_proc_t*)opal_pointer_array_get_item(orte_local_children, i))) {
                continue;
            }
            if (!ORTE_FLAG_TEST(child, ORTE_PROC_IS_ALIVE)) {
                continue;
            }
            if (0 == child->pid) {
                /* race condition */
                continue;
            }
            if (NULL == (st = (opal_pstats_t*)opal_ring_buffer_poke(&child->stats, -1))) {
                continue;
            }
            OPAL_OUTPUT_VERBOSE((5, orcm_sensor_base_framework.framework_output,
                                 "%s PROC %s AT VSIZE %f",
                                 ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                                 ORTE_NAME_PRINT(&child->name), st->vsize));
            if (mca_sensor_resusage_component.proc_memory_limit <= st->vsize) {
                /* report the problem */
                ORTE_ACTIVATE_PROC_STATE(&child->name, ORTE_PROC_STATE_SENSOR_BOUND_EXCEEDED);
            }
        }
    }
#endif
}

static void mycleanup(int dbhandle, int status, opal_list_t *kvs,
                      opal_list_t *ret, void *cbdata)
{
    OPAL_LIST_RELEASE(kvs);
    if (ORTE_SUCCESS != status) {
        log_enabled = false;
    }
}
static void mycleanup_procstat(int dbhandle, int status, opal_list_t *kvs,
                               opal_list_t *ret, void *cbdata)
{
    OPAL_LIST_RELEASE(kvs);
    if (ORTE_SUCCESS != status) {
        log_enabled = false;
    }
    if(NULL != cbdata)
        free(cbdata);
}

static void res_log(opal_buffer_t *sample)
{
    opal_pstats_t *st=NULL;
    opal_node_stats_t *nst=NULL;
    int rc, n;
    opal_list_t *vals;
    opal_value_t *kv;
    char *node;
    struct timeval sampletime;
    char *primary_key;
    orcm_metric_value_t *sensor_metric;

    if (!log_enabled) {
        return;
    }

    /* unpack the node name */
    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &node, &n, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        return;
    }

    /* sample time */
    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &sampletime, &n, OPAL_TIMEVAL))) {
        ORTE_ERROR_LOG(rc);
        return;
    }

    /* unpack the node stats */
    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &nst, &n, OPAL_NODE_STAT))) {
        ORTE_ERROR_LOG(rc);
        return;
    }

    if (mca_sensor_resusage_component.log_node_stats) {
        vals = OBJ_NEW(opal_list_t);

        kv = OBJ_NEW(opal_value_t);
        kv->key = strdup("ctime");
        kv->type = OPAL_TIMEVAL;
        kv->data.tv = sampletime;
        opal_list_append(vals, &kv->super);

        kv = OBJ_NEW(opal_value_t);
        kv->key = strdup("hostname");
        kv->type = OPAL_STRING;
        kv->data.string = strdup(node);
        opal_list_append(vals, &kv->super);

        kv = OBJ_NEW(opal_value_t);
        if (NULL == kv) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        kv->key = strdup("data_group");
        kv->type = OPAL_STRING;
        kv->data.string = strdup("nodestats");
        opal_list_append(vals, &kv->super);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        sensor_metric->value.key = strdup("total_mem");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = nst->total_mem;
        sensor_metric->units = strdup("MB");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        sensor_metric->value.key = strdup("free_mem");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = nst->free_mem;
        sensor_metric->units = strdup("MB");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        sensor_metric->value.key = strdup("buffers");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = nst->buffers;
        sensor_metric->units = strdup("MB");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        sensor_metric->value.key = strdup("cached");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = nst->cached;
        sensor_metric->units = strdup("MB");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        sensor_metric->value.key = strdup("swap_total");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = nst->swap_total;
        sensor_metric->units = strdup("MB");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        sensor_metric->value.key = strdup("swap_free");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = nst->swap_free;
        sensor_metric->units = strdup("MB");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        sensor_metric->value.key = strdup("mapped");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = nst->mapped;
        sensor_metric->units = strdup("MB");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        sensor_metric->value.key = strdup("swap_cached");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = nst->swap_cached;
        sensor_metric->units = strdup("MB");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        sensor_metric->value.key = strdup("la");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = nst->la;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        sensor_metric->value.key = strdup("la5");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = nst->la5;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            return;
        }
        sensor_metric->value.key = strdup("la15");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = nst->la15;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* store it */
        if (0 <= orcm_sensor_base.dbhandle) {
            orcm_db.store_new(orcm_sensor_base.dbhandle, ORCM_DB_ENV_DATA, vals, NULL, mycleanup, NULL);
        } else {
            OPAL_LIST_RELEASE(vals);
        }
    }

    if (mca_sensor_resusage_component.log_process_stats) {
        /* unpack all process stats */
        n=1;
        while (OPAL_SUCCESS == (rc = opal_dss.unpack(sample, &st, &n, OPAL_PSTAT))) {
            vals = OBJ_NEW(opal_list_t);
            asprintf(&primary_key, "procstat_%s",st->cmd);
	    if (NULL == primary_key){
                goto error;
	    }

            kv = OBJ_NEW(opal_value_t);
            kv->key = strdup("ctime");
            kv->type = OPAL_TIMEVAL;
            kv->data.tv = sampletime;
            opal_list_append(vals, &kv->super);

            kv = OBJ_NEW(opal_value_t);
            kv->key = strdup("hostname");
            kv->type = OPAL_STRING;
            kv->data.string = strdup(node);
            opal_list_append(vals, &kv->super);

            kv = OBJ_NEW(opal_value_t);
            if (NULL == kv) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            kv->key = strdup("data_group");
            kv->type = OPAL_STRING;
            kv->data.string = strdup(primary_key);
            opal_list_append(vals, &kv->super);

            sensor_metric = OBJ_NEW(orcm_metric_value_t);
            if (NULL == sensor_metric) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            sensor_metric->value.key = strdup("rank");
            sensor_metric->value.type = OPAL_INT32;
            sensor_metric->value.data.int32 = st->rank;
            sensor_metric->units = NULL;
            opal_list_append(vals, (opal_list_item_t *)sensor_metric);

            sensor_metric = OBJ_NEW(orcm_metric_value_t);
            if (NULL == sensor_metric) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            sensor_metric->value.key = strdup("pid");
            sensor_metric->value.type = OPAL_PID;
            sensor_metric->value.data.pid = st->pid;
            sensor_metric->units = NULL;
            opal_list_append(vals, (opal_list_item_t *)sensor_metric);

            sensor_metric = OBJ_NEW(orcm_metric_value_t);
            if (NULL == sensor_metric) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            sensor_metric->value.key = strdup("cmd");
            sensor_metric->value.type = OPAL_STRING;
            sensor_metric->value.data.string = strdup(st->cmd);
            sensor_metric->units = NULL;
            opal_list_append(vals, (opal_list_item_t *)sensor_metric);

            sensor_metric = OBJ_NEW(orcm_metric_value_t);
            if (NULL == sensor_metric) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            sensor_metric->value.key = strdup("state");
            sensor_metric->value.type = OPAL_STRING;
            sensor_metric->value.data.string = (char*)malloc(3 * sizeof(char));
            if (NULL == sensor_metric->value.data.string) {
                goto error;
            }
            sensor_metric->value.data.string[0] = st->state[0];
            sensor_metric->value.data.string[1] = st->state[1];
            sensor_metric->value.data.string[2] = '\0';
            sensor_metric->units = NULL;
            opal_list_append(vals, (opal_list_item_t *)sensor_metric);

            sensor_metric = OBJ_NEW(orcm_metric_value_t);
            if (NULL == sensor_metric) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            sensor_metric->value.key = strdup("percent_cpu");
            sensor_metric->value.type = OPAL_FLOAT;
            sensor_metric->value.data.fval = st->percent_cpu;
            sensor_metric->units = strdup("%");
            opal_list_append(vals, (opal_list_item_t *)sensor_metric);

            sensor_metric = OBJ_NEW(orcm_metric_value_t);
            if (NULL == sensor_metric) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            sensor_metric->value.key = strdup("priority");
            sensor_metric->value.type = OPAL_INT32;
            sensor_metric->value.data.int32 = st->priority;
            sensor_metric->units = NULL;
            opal_list_append(vals, (opal_list_item_t *)sensor_metric);

            sensor_metric = OBJ_NEW(orcm_metric_value_t);
            if (NULL == sensor_metric) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            sensor_metric->value.key = strdup("num_threads");
            sensor_metric->value.type = OPAL_INT16;
            sensor_metric->value.data.int16 = st->num_threads;
            sensor_metric->units = NULL;
            opal_list_append(vals, (opal_list_item_t *)sensor_metric);

            sensor_metric = OBJ_NEW(orcm_metric_value_t);
            if (NULL == sensor_metric) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            sensor_metric->value.key = strdup("vsize");
            sensor_metric->value.type = OPAL_FLOAT;
            sensor_metric->value.data.fval = st->vsize;
            sensor_metric->units = strdup("MB");
            opal_list_append(vals, (opal_list_item_t *)sensor_metric);

            sensor_metric = OBJ_NEW(orcm_metric_value_t);
            if (NULL == sensor_metric) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            sensor_metric->value.key = strdup("rss");
            sensor_metric->value.type = OPAL_FLOAT;
            sensor_metric->value.data.fval = st->rss;
            sensor_metric->units = strdup("MB");
            opal_list_append(vals, (opal_list_item_t *)sensor_metric);

            sensor_metric = OBJ_NEW(orcm_metric_value_t);
            if (NULL == sensor_metric) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            sensor_metric->value.key = strdup("peak_vsize");
            sensor_metric->value.type = OPAL_FLOAT;
            sensor_metric->value.data.fval = st->peak_vsize;
            sensor_metric->units = strdup("MB");
            opal_list_append(vals, (opal_list_item_t *)sensor_metric);

            sensor_metric = OBJ_NEW(orcm_metric_value_t);
            if (NULL == sensor_metric) {
                ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
                goto error;
            }
            sensor_metric->value.key = strdup("processor");
            sensor_metric->value.type = OPAL_INT16;
            sensor_metric->value.data.int16 = st->processor;
            sensor_metric->units = NULL;
            opal_list_append(vals, (opal_list_item_t *)sensor_metric);

            /* store it */
            if (0 <= orcm_sensor_base.dbhandle) {
                orcm_db.store_new(orcm_sensor_base.dbhandle, ORCM_DB_ENV_DATA, vals, NULL, mycleanup_procstat, primary_key);
            } else {
                OPAL_LIST_RELEASE(vals);
                if(primary_key != NULL) {
                    free(primary_key);
                }
            }
            OBJ_RELEASE(st);
            n=1;
        }
        if (OPAL_ERR_UNPACK_READ_PAST_END_OF_BUFFER != rc) {
            ORTE_ERROR_LOG(rc);
        }
        if (mca_sensor_resusage_component.log_node_stats) {
            OBJ_RELEASE(nst);
        }
    }
    return;

error:
       if (NULL != primary_key) {
           free (primary_key);
       }
       if (NULL != vals) {
           OBJ_RELEASE(vals);
       }
}

static void generate_test_vector(opal_buffer_t *v)
{
    int ret;
    time_t now;
    char *ctmp, *timestamp_str;
    char time_str[40];
    struct tm *sample_time;
    opal_pstats_t *stats;
    opal_node_stats_t *nstats;

    /* pack the plugin name */
    ctmp = strdup("resusage");
    if (OPAL_SUCCESS != (ret = opal_dss.pack(v, &ctmp, 1, OPAL_STRING))){
    ORTE_ERROR_LOG(ret);
    OBJ_DESTRUCT(&v);
    free(ctmp);
    return;
    }
    free(ctmp);

    /* pack the hostname */
    if (OPAL_SUCCESS != (ret =
                opal_dss.pack(v, &orte_process_info.nodename, 1, OPAL_STRING))){
        ORTE_ERROR_LOG(ret);
        OBJ_DESTRUCT(&v);
        return;
    }

    /* get the sample time */
    now = time(NULL);
    /* pass the time along as a simple string */
    sample_time = localtime(&now);
    if (NULL == sample_time) {
        ORTE_ERROR_LOG(OPAL_ERR_BAD_PARAM);
        return;
    }
    strftime(time_str, sizeof(time_str), "%F %T%z", sample_time);
    asprintf(&timestamp_str, "%s", time_str);
    if (OPAL_SUCCESS != (ret = opal_dss.pack(v, &timestamp_str, 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(ret);
        OBJ_DESTRUCT(&v);
        free(timestamp_str);
        return;
    }
    free(timestamp_str);

    /* update stats on ourself and the node */
    stats = OBJ_NEW(opal_pstats_t);
    nstats = OBJ_NEW(opal_node_stats_t);
    if (ORCM_SUCCESS != (ret = opal_pstat.query(orte_process_info.pid, stats, nstats))) {
        ORTE_ERROR_LOG(ret);
        OBJ_RELEASE(stats);
        OBJ_RELEASE(nstats);
        OBJ_DESTRUCT(&v);
        return;
    }

    /* will overwrite only node stats because actual process stats are needed for performance */
    if (NULL != nstats) {
    /* set the memory values to something identifiable for testing */
            nstats->total_mem = 100.1;
            nstats->free_mem = 200.2;
            nstats->buffers = 300.3;
            nstats->cached = 400.4;
            nstats->swap_cached = 500.5;
            nstats->swap_total = 600.6;
            nstats->swap_free = 700.7;
            nstats->mapped = 800.8;
            /* set the load averages */
            nstats->la = 1.0;
            nstats->la5 = 5.0;
            nstats->la15 = 15.0;
    }

    /* the stats framework can't know nodename or rank */
    strncpy(stats->node, orte_process_info.nodename, (OPAL_PSTAT_MAX_STRING_LEN - 1));
    stats->rank = ORTE_PROC_MY_NAME->vpid;

    /* pack the node stats */
    if (OPAL_SUCCESS != (ret = opal_dss.pack(v, &nstats, 1, OPAL_NODE_STAT))) {
        ORTE_ERROR_LOG(ret);
        OBJ_DESTRUCT(&v);
        return;
    }

    /* pack the process stats */
    if (OPAL_SUCCESS != (ret = opal_dss.pack(v, &stats, 1, OPAL_PSTAT))) {
        ORTE_ERROR_LOG(ret);
        OBJ_DESTRUCT(&v);
        return;
    }

    opal_output_verbose(5,orcm_sensor_base_framework.framework_output,
            "%s sensor:resusage: Test vector called",
            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
}
