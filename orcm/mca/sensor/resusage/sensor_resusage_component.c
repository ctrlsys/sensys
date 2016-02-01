/*
 * Copyright (c) 2010-2011 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2014-2016 Intel, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orcm_config.h"
#include "orcm/constants.h"

#include "opal/mca/base/base.h"
#include "opal/util/output.h"

#include "sensor_resusage.h"

/*
 * Local functions
 */
static int orcm_sensor_resusage_register (void);
static int orcm_sensor_resusage_open(void);
static int orcm_sensor_resusage_close(void);
static int orcm_sensor_resusage_query(mca_base_module_t **module, int *priority);

orcm_sensor_resusage_component_t mca_sensor_resusage_component = {
    {
        {
            ORCM_SENSOR_BASE_VERSION_1_0_0,
            /* Component name and version */
            .mca_component_name = "resusage",
            MCA_BASE_MAKE_VERSION(component, ORCM_MAJOR_VERSION, ORCM_MINOR_VERSION,
                                  ORCM_RELEASE_VERSION),

            /* Component open and close functions */
            .mca_open_component = orcm_sensor_resusage_open,
            .mca_close_component = orcm_sensor_resusage_close,
            .mca_query_component = orcm_sensor_resusage_query,
            .mca_register_component_params = orcm_sensor_resusage_register
        },
        .base_data = {
            /* The component is checkpoint ready */
            MCA_BASE_METADATA_PARAM_CHECKPOINT
        },
        "procresource,noderesource"
    }
};

static int node_memory_limit;
static int proc_memory_limit;

/**
  * component open/close/init function
  */
static int orcm_sensor_resusage_register (void)
{
    mca_base_component_t *c = &mca_sensor_resusage_component.super.base_version;

    mca_sensor_resusage_component.sample_rate = 0;
    (void) mca_base_component_var_register (c, "sample_rate", "Sample rate in seconds (default: 0)",
                                            MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_READONLY,
                                            &mca_sensor_resusage_component.sample_rate);
    if (mca_sensor_resusage_component.sample_rate < 0) {
        opal_output(0, "Illegal value %d - must be > 0", mca_sensor_resusage_component.sample_rate);
        return ORCM_ERR_BAD_PARAM;
    }

    node_memory_limit = 0;
    (void) mca_base_component_var_register (c, "node_memory_limit",
                                            "Percentage of total memory that can be in-use",
                                            MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_READONLY,
                                            &node_memory_limit);
    mca_sensor_resusage_component.node_memory_limit = (float)node_memory_limit/100.0;

    proc_memory_limit = 0;
    (void) mca_base_component_var_register (c, "proc_memory_limit",
                                            "Max virtual memory size in MBytes",
                                            MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_READONLY,
                                            &proc_memory_limit);
    mca_sensor_resusage_component.proc_memory_limit = (float) proc_memory_limit;

    mca_sensor_resusage_component.log_node_stats = false;
    (void) mca_base_component_var_register (c, "log_node_stats", "Log the node stats",
                                            MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_READONLY,
                                            &mca_sensor_resusage_component.log_node_stats);

    mca_sensor_resusage_component.log_process_stats = false;
    (void) mca_base_component_var_register (c, "log_process_stats", "Log the process stats",
                                            MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_READONLY,
                                            &mca_sensor_resusage_component.log_process_stats);

    mca_sensor_resusage_component.test = false;
    (void) mca_base_component_var_register (c, "test",
                                            "Generate and pass test vector",
                                            MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_READONLY,
                                            & mca_sensor_resusage_component.test);

    mca_sensor_resusage_component.collect_metrics = true;
    (void) mca_base_component_var_register(c, "collect_metrics",
                                           "Enable metric collection for the resusage plugin",
                                           MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0,
                                           OPAL_INFO_LVL_9,
                                           MCA_BASE_VAR_SCOPE_READONLY,
                                           &mca_sensor_resusage_component.collect_metrics);

    return ORCM_SUCCESS;
}

static int orcm_sensor_resusage_open(void)
{
    if (mca_sensor_resusage_component.sample_rate < 0) {
        opal_output(0, "Illegal value %d - must be > 0", mca_sensor_resusage_component.sample_rate);
        return ORCM_ERR_FATAL;
    }

    mca_sensor_resusage_component.node_memory_limit = (float) node_memory_limit/100.0;
    mca_sensor_resusage_component.proc_memory_limit = (float) proc_memory_limit;

    return ORCM_SUCCESS;
}


static int orcm_sensor_resusage_query(mca_base_module_t **module, int *priority)
{
    *priority = 100;  /* ahead of heartbeat */
    *module = (mca_base_module_t *)&orcm_sensor_resusage_module;

    return ORCM_SUCCESS;
}

/**
 *  Close all subsystems.
 */

static int orcm_sensor_resusage_close(void)
{
    return ORCM_SUCCESS;
}
