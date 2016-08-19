/*
 * Copyright (c) 2015      Intel, Inc. All rights reserved.
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

#ifndef MCA_analytics_extension_EXPORT_H
#define MCA_analytics_extension_EXPORT_H

#include "orcm_config.h"

#include "orcm/mca/analytics/analytics.h"

BEGIN_C_DECLS

/*
 * Local Component structures
 */

ORCM_MODULE_DECLSPEC extern orcm_analytics_base_component_t mca_analytics_extension_component;

typedef struct {
    orcm_analytics_base_module_t api;
} mca_analytics_extension_module_t;
ORCM_DECLSPEC extern mca_analytics_extension_module_t orcm_analytics_extension_module;

END_C_DECLS

#endif /* MCA_analytics_extension_EXPORT_H */
