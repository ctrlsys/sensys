/*
 * Copyright (c) 2010      Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2012-2013 Los Alamos National Security, Inc.  All rights reserved.
 * Copyright (c) 2013      Intel Corporation. All rights reserved.
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orcm_config.h"
#include "orcm/constants.h"

#include "opal/class/opal_list.h"
#include "opal/mca/mca.h"
#include "opal/mca/base/base.h"
#include "opal/mca/base/mca_base_component_repository.h"
#include "opal/util/output.h"

#include "orcm/mca/db/base/base.h"

bool orcm_db_base_selected = false;

int orcm_db_base_select(void)
{
    mca_base_component_list_item_t *cli;
    orcm_db_base_component_t *component;
    orcm_db_base_active_component_t *active, *ncomponent;
    bool inserted;

    if (orcm_db_base_selected) {
        /* ensure we don't do this twice */
        return ORCM_SUCCESS;
    }
    orcm_db_base_selected = true;

    /* Query all available components and ask if they have a module */
    OPAL_LIST_FOREACH(cli, &orcm_db_base_framework.framework_components, mca_base_component_list_item_t) {
        component = (orcm_db_base_component_t*)cli->cli_component;

        opal_output_verbose(5, orcm_db_base_framework.framework_output,
                            "mca:db:select: checking available component %s",
                            component->base_version.mca_component_name);

        /* If there's no query function, skip it */
        if (NULL == component->available) {
            opal_output_verbose(5, orcm_db_base_framework.framework_output,
                                "mca:db:select: Skipping component [%s]. It does not implement a query function",
                                component->base_version.mca_component_name );
            continue;
        }

        /* Query the component */
        opal_output_verbose(5, orcm_db_base_framework.framework_output,
                            "mca:db:select: Querying component [%s]",
                            component->base_version.mca_component_name);

        /* If the component is not available, then skip it as
         * it has no available interfaces
         */
        if (!component->available()) {
            opal_output_verbose(5, orcm_db_base_framework.framework_output,
                                "mca:db:select: Skipping component [%s] - not available",
                                component->base_version.mca_component_name );
            continue;
        }

        /* maintain priority order */
        inserted = false;
        ncomponent = OBJ_NEW(orcm_db_base_active_component_t);
        ncomponent->component = component;
        OPAL_LIST_FOREACH(active, &orcm_db_base.actives, orcm_db_base_active_component_t) {
            if (component->priority > active->component->priority) {
                opal_list_insert_pos(&orcm_db_base.actives,
                                     &active->super, &ncomponent->super);
                inserted = true;
                break;
            }
        }
        if (!inserted) {
            /* must be lowest priority - add to end */
            opal_list_append(&orcm_db_base.actives, &ncomponent->super);
        }
    }

    /* if no components are available, that is an error */
    if (0 == opal_list_get_size(&orcm_db_base.actives)) {
        return ORCM_ERR_NOT_FOUND;
    }

    if (4 < opal_output_get_verbosity(orcm_db_base_framework.framework_output)) {
        opal_output(0, "Final db priorities");
        /* show the prioritized list */
        OPAL_LIST_FOREACH(active, &orcm_db_base.actives, orcm_db_base_active_component_t) {
            opal_output(0, "\tComponent: %s Store Priority: %d",
                        active->component->base_version.mca_component_name,
                        active->component->priority);
        }
    }

    return ORCM_SUCCESS;;
}
