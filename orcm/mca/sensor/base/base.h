/*
 * Copyright (c) 2009      Cisco Systems, Inc.  All rights reserved. 
 * Copyright (c) 2013      Los Alamos National Security, LLC.  All rights reserved.
 * Copyright (c) 2014      Intel Corporation.  All rights reserved.
 *
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
/** @file:
 */

#ifndef MCA_SENSOR_BASE_H
#define MCA_SENSOR_BASE_H

/*
 * includes
 */
#include "orcm_config.h"

#include "opal/class/opal_list.h"
#include "opal/mca/base/base.h"

#include "orcm/mca/sensor/sensor.h"

BEGIN_C_DECLS

/*
 * MCA Framework
 */
ORCM_DECLSPEC extern mca_base_framework_t orcm_sensor_base_framework;
/* select a component */
ORCM_DECLSPEC int orcm_sensor_base_select(void);

END_C_DECLS
#endif
