/*
 * Copyright (c) 2013-2014 Intel Corporation. All rights reserved.
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
/** @file:
 */

#ifndef MCA_SST_BASE_H
#define MCA_SST_BASE_H

/*
 * includes
 */
#include "orcm_config.h"

#include "opal/mca/mca.h"
#include "opal/mca/base/base.h"

#include "orte/types.h"

#include "orcm/mca/sst/sst.h"


BEGIN_C_DECLS


/*
 * MCA framework
 */
ORCM_DECLSPEC extern mca_base_framework_t orcm_sst_base_framework;
/*
 * Select an available component.
 */
ORCM_DECLSPEC int orcm_sst_base_select(void);

END_C_DECLS
#endif
