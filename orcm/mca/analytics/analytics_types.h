/*
 * Copyright (c) 2014      Intel, Inc. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#ifndef MCA_ANALYTICS_TYPES_H
#define MCA_ANALYTICS_TYPES_H

/*
 * includes
 */

#include "orcm_config.h"
#include "orcm/constants.h"

#include "opal/class/opal_object.h"
#include "opal/class/opal_value_array.h"
#include "opal/class/opal_list.h"
#include "opal/mca/event/event.h"
#include "orcm/runtime/orcm_globals.h"

BEGIN_C_DECLS

struct orcm_analytics_base_module;

typedef struct orcm_analytics_base_module orcm_analytics_base_module_t;

/* define a workflow "step" object - this object
 * specifies what operation is to be performed
 * on the input */
typedef struct {
    opal_list_item_t super;
    opal_list_t attributes;
    char *analytic;
    orcm_analytics_base_module_t *mod;
} orcm_workflow_step_t;
OBJ_CLASS_DECLARATION(orcm_workflow_step_t);

/* define a workflow object */
typedef struct {
    opal_list_item_t super;
    char *name;
    int workflow_id;
    opal_list_t steps;
    opal_event_base_t *ev_base;
    bool ev_active;
} orcm_workflow_t;
OBJ_CLASS_DECLARATION(orcm_workflow_t);

typedef struct {
    opal_object_t super;
    opal_list_t *key;
    opal_list_t *non_compute_data;
    opal_list_t *compute_data;
} orcm_analytics_value_t;
OBJ_CLASS_DECLARATION(orcm_analytics_value_t);

/* define a workflow caddy object */
typedef struct {
    opal_object_t super;
    opal_event_t ev;
    orcm_workflow_step_t *wf_step;
    orcm_workflow_t    *wf;
    uint64_t hash_key;
    orcm_analytics_value_t *analytics_value;
    orcm_analytics_base_module_t *imod;
} orcm_workflow_caddy_t;
OBJ_CLASS_DECLARATION(orcm_workflow_caddy_t);

/* define a few commands */
typedef uint8_t orcm_analytics_cmd_flag_t;
#define ORCM_ANALYTICS_CMD_T OPAL_UINT8

typedef enum {
    ORCM_ANALYTICS_WORKFLOW_CREATE = 1,
    ORCM_ANALYTICS_WORKFLOW_DELETE,
    ORCM_ANALYTICS_WORKFLOW_LIST,
    ORCM_ANALYTICS_WORFLOW_ERROR
}orcm_analytics_workflow_commands;

END_C_DECLS

#endif
