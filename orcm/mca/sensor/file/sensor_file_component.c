/*
 * Copyright (c) 2010      Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2013-2016 Intel Corporation. All rights reserved.
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orcm_config.h"
#include "orcm/constants.h"
#include "orcm/mca/sensor/base/sensor_private.h"

#include "opal/mca/base/base.h"
#include "opal/mca/base/mca_base_var.h"

#include "sensor_file.h"

extern orcm_sensor_base_t orcm_sensor_base;

/*
 * Local functions
 */
static int orcm_sensor_file_register (void);
static int orcm_sensor_file_open(void);
static int orcm_sensor_file_close(void);
static int orcm_sensor_file_query(mca_base_module_t **module, int *priority);

orcm_sensor_file_component_t mca_sensor_file_component = {
    {
        {
            ORCM_SENSOR_BASE_VERSION_1_0_0,
            /* Component name and version */
            .mca_component_name = "file",
            MCA_BASE_MAKE_VERSION(component, ORCM_MAJOR_VERSION, ORCM_MINOR_VERSION,
                                  ORCM_RELEASE_VERSION),

            /* Component open and close functions */
            .mca_open_component = orcm_sensor_file_open,
            .mca_close_component = orcm_sensor_file_close,
            .mca_query_component = orcm_sensor_file_query,
            .mca_register_component_params = orcm_sensor_file_register
        },
        .base_data = {
            /* The component is checkpoint ready */
            MCA_BASE_METADATA_PARAM_CHECKPOINT
        },
        "filemods"  // data being sensed
    }
};


/**
  * component register/open/close/init function
  */
static int orcm_sensor_file_register (void)
{
    mca_base_component_t *c = &mca_sensor_file_component.super.base_version;

    /* lookup parameters */
    mca_sensor_file_component.file = NULL;
    (void) mca_base_component_var_register (c, "filename", "File to be monitored",
                                            MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_ALL_EQ,
                                            &mca_sensor_file_component.file);

    mca_sensor_file_component.check_size = false;
    (void) mca_base_component_var_register (c, "check_size", "Check the file size",
                                            MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_ALL_EQ,
                                            &mca_sensor_file_component.check_size);

    mca_sensor_file_component.check_access = false;
    (void) mca_base_component_var_register (c, "check_access", "Check access time",
                                            MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_ALL_EQ,
                                            &mca_sensor_file_component.check_access);

    mca_sensor_file_component.check_mod = false;
    (void) mca_base_component_var_register (c, "check_mod", "Check modification time",
                                            MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_ALL_EQ,
                                            &mca_sensor_file_component.check_mod);

    mca_sensor_file_component.limit = 3;
    (void) mca_base_component_var_register (c, "limit",
                                            "Number of times the sensor can detect no motion before declaring error (default=3)",
                                            MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                            OPAL_INFO_LVL_9,
                                            MCA_BASE_VAR_SCOPE_ALL_EQ,
                                            &mca_sensor_file_component.limit);

    mca_sensor_file_component.use_progress_thread = false;
    (void) mca_base_component_var_register(c, "use_progress_thread",
                                           "Use a dedicated progress thread for file sensors [default: false]",
                                           MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0,
                                           OPAL_INFO_LVL_9,
                                           MCA_BASE_VAR_SCOPE_READONLY,
                                           &mca_sensor_file_component.use_progress_thread);

    mca_sensor_file_component.sample_rate = 0;
    (void) mca_base_component_var_register(c, "sample_rate",
                                           "Sample rate in seconds",
                                           MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                           OPAL_INFO_LVL_9,
                                           MCA_BASE_VAR_SCOPE_READONLY,
                                           &mca_sensor_file_component.sample_rate);

    mca_sensor_file_component.collect_metrics = orcm_sensor_base.collect_metrics;
    (void) mca_base_component_var_register(c, "collect_metrics",
                                           "Enable metric collection for the file plugin",
                                           MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0,
                                           OPAL_INFO_LVL_9,
                                           MCA_BASE_VAR_SCOPE_READONLY,
                                           &mca_sensor_file_component.collect_metrics);

    return ORCM_SUCCESS;
}

static int orcm_sensor_file_open(void)
{
    return ORCM_SUCCESS;
}


static int orcm_sensor_file_query(mca_base_module_t **module, int *priority)
{
    *priority = 20;  /* higher than heartbeat */
    *module = (mca_base_module_t *)&orcm_sensor_file_module;
    return ORCM_SUCCESS;
}

/**
 *  Close all subsystems.
 */

static int orcm_sensor_file_close(void)
{
    return ORCM_SUCCESS;
}
