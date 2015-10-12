/*
 * Copyright (c) 2014      Intel, Inc. All rights reserved.
 *
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

/**
 * @file
 * 
 */

#ifndef MCA_analytics_average_EXPORT_H
#define MCA_analytics_average_EXPORT_H

#include "orcm_config.h"

#include "orcm/mca/analytics/analytics.h"

BEGIN_C_DECLS

/*
 * Local Component structures
 */

typedef struct
{
    opal_object_t super;
    unsigned int num_sample;
    orcm_value_t *value_average;
} orcm_mca_analytics_average_item_value;

OBJ_CLASS_DECLARATION(orcm_mca_analytics_average_item_value);

ORCM_MODULE_DECLSPEC extern orcm_analytics_base_component_t mca_analytics_average_component;

typedef struct {
    orcm_analytics_base_module_t api;
} mca_analytics_average_module_t;
ORCM_DECLSPEC extern mca_analytics_average_module_t orcm_analytics_average_module;

END_C_DECLS

#endif /* MCA_analytics_threshold_EXPORT_H */
