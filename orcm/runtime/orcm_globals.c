/*
 * Copyright (c) 2009-2011 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2013-2014 Intel Corporation.  All rights reserved.
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orcm_config.h"

#include "opal/dss/dss.h"

#include "orte/mca/errmgr/errmgr.h"

#include "orcm/runtime/orcm_globals.h"
#include "orcm/mca/scd/base/base.h"

int orcm_dt_init(void)
{
    int rc;
    opal_data_type_t tmp;

    /* register the scheduler types for packing/unpacking services */
    tmp = ORCM_ALLOC;
    if (OPAL_SUCCESS !=
        (rc = opal_dss.register_type(orcm_pack_alloc,
                                     orcm_unpack_alloc,
                                     (opal_dss_copy_fn_t)orcm_copy_alloc,
                                     (opal_dss_compare_fn_t)orcm_compare_alloc,
                                     (opal_dss_print_fn_t)orcm_print_alloc,
                                     OPAL_DSS_STRUCTURED,
                                     "ORCM_ALLOC", &tmp))) {
        ORTE_ERROR_LOG(rc);
        return rc;
    }
    
    return ORCM_SUCCESS;
}

int orcm_set_proc_hostname(char *hostname)
{
    int rc = ORCM_SUCCESS;

    if (NULL == orcm_proc_hostname) {
        orcm_proc_hostname = strdup(hostname);
        if (NULL == orcm_proc_hostname) {
            rc = ORCM_ERR_OUT_OF_RESOURCE;
        }
    }

    return rc;
}

char *orcm_get_proc_hostname()
{
    return orcm_proc_hostname;
}

const char *orcm_node_state_to_str(orcm_node_state_t state)
{
    char *s;
    
    switch (state) {
        case ORCM_NODE_STATE_UNDEF:
            s = "UNDEF";
            break;
        case ORCM_NODE_STATE_UNKNOWN:
            s = "UNKNOWN";
            break;
        case ORCM_NODE_STATE_UP:
            s = "UP";
            break;
        case ORCM_NODE_STATE_DOWN:
            s = "DOWN";
            break;
        case ORCM_NODE_STATE_SESTERM:
            s = "SESSION TERMINATING";
            break;
        case ORCM_NODE_STATE_DRAIN:
            s = "DRAIN";
            break;
        case ORCM_NODE_STATE_RESUME:
            s = "RESUME";
            break;
        default:
            s = "STATEUNDEF";
    }
    return s;
}

const char *orcm_node_state_to_char(orcm_node_state_t state)
{
    char *s;
    
    switch (state) {
        case ORCM_NODE_STATE_UNKNOWN:
            s = "?";
            break;
        case ORCM_NODE_STATE_UP:
            s = "U";
            break;
        case ORCM_NODE_STATE_DOWN:
            s = "D";
            break;
        case ORCM_NODE_STATE_SESTERM:
            s = "T";
            break;
        case ORCM_NODE_STATE_DRAIN:
            s = "X";
            break;
        case ORCM_NODE_STATE_RESUME:
            s = "R";
            break;
        default:
            s = "?";
    }
    return s;
}

static void omv_constructor(orcm_value_t *omvp)
{
    omvp->units = NULL;
}

static void omv_destructor(orcm_value_t *omvp)
{
    if (NULL != omvp->units) {
        free(omvp->units);
    }
}

OBJ_CLASS_INSTANCE(orcm_value_t,
                   opal_value_t,
                   omv_constructor,
                   omv_destructor);
