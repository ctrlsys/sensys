/*
 * Copyright (c) 2004-2010 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2013 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2007      Sun Microsystems, Inc.  All rights reserved.
 * Copyright (c) 2007      Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2010      Oracle and/or its affiliates.  All rights reserved.
 * Copyright (c) 2014-2016 Intel Corporation.  All rights reserved.
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include <math.h>

#include "orcm/tools/octl/common.h"
#include "orcm/util/attr.h"
#include "orcm/util/utils.h"
#include "orcm/mca/scd/base/base.h"
#include "orcm/mca/pwrmgmt/pwrmgmt.h"

int orcm_octl_session_status(char **argv)
{
    return ORCM_ERR_NOT_IMPLEMENTED;
}

int orcm_octl_session_cancel(char **argv)
{
    orcm_scd_cmd_flag_t command;
    orcm_alloc_id_t id;
    opal_buffer_t *buf;
    orte_rml_recv_cb_t xfer;
    long session;
    int rc, n, result;

    if (3 != opal_argv_count(argv)) {
        orcm_octl_usage("session-cancel", INVALID_USG);
        return ORCM_ERR_BAD_PARAM;
    }
    session = strtol(argv[2], NULL, 10);
    // FIXME: validate session id better
    if (session > 0) {
        /* setup to receive the result */
        OBJ_CONSTRUCT(&xfer, orte_rml_recv_cb_t);
        xfer.active = true;
        orte_rml.recv_buffer_nb(ORTE_NAME_WILDCARD,
                                ORCM_RML_TAG_SCD,
                                ORTE_RML_NON_PERSISTENT,
                                orte_rml_recv_callback, &xfer);

        /* send it to the scheduler */
        buf = OBJ_NEW(opal_buffer_t);

        command = ORCM_SESSION_CANCEL_COMMAND;
        /* pack the cancel command flag */
        if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &command,
                                                1, ORCM_SCD_CMD_T))) {
            orcm_octl_error("pack");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }

        id = (orcm_alloc_id_t)session;
        /* pack the session id */
        if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &id, 1, ORCM_ALLOC_ID_T))) {
            orcm_octl_error("pack");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }
        if (ORTE_SUCCESS != (rc = orte_rml.send_buffer_nb(ORTE_PROC_MY_SCHEDULER,
                                                          buf,
                                                          ORCM_RML_TAG_SCD,
                                                          orte_rml_send_callback,
                                                          NULL))) {
            orcm_octl_error("connection-fail");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }
        /* get result */
        n=1;
        ORCM_WAIT_FOR_COMPLETION(xfer.active, ORCM_OCTL_WAIT_TIMEOUT, &rc);
        if (ORCM_SUCCESS != rc) {
            orcm_octl_error("connection-fail");
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &result,
                                                  &n, OPAL_INT))) {
            orcm_octl_error("unpack");
            OBJ_DESTRUCT(&xfer);
            return rc;
        }
        if (0 == result) {
            orcm_octl_info("success");
        } else {
            orcm_octl_error("session-cancel");
            return result;
        }
    } else {
        orcm_octl_error("session-id");
        return ORCM_ERROR;
    }

    return ORCM_SUCCESS;
}

int orcm_octl_session_set(int cmd, char **argv)
{
    orcm_scd_cmd_flag_t command;
    orcm_alloc_id_t id;
    opal_buffer_t *buf;
    orte_rml_recv_cb_t xfer;
    int rc, n, result;
    int32_t int_param;
    long session;
    float float_param;
    bool bool_param;
    bool per_session = true;

    if (5 != opal_argv_count(argv)) {
        orcm_octl_usage("session-set", INVALID_USG);
        return ORCM_ERR_BAD_PARAM;
    }

    session = strtol(argv[3], NULL, 10);

    /* setup to receive the result */
    OBJ_CONSTRUCT(&xfer, orte_rml_recv_cb_t);
    xfer.active = true;
    orte_rml.recv_buffer_nb(ORTE_NAME_WILDCARD,
                            ORCM_RML_TAG_SCD,
                            ORTE_RML_NON_PERSISTENT,
                            orte_rml_recv_callback, &xfer);

    /* send it to the scheduler */
    buf = OBJ_NEW(opal_buffer_t);

    command = ORCM_SET_POWER_COMMAND;
    /* pack the command flag */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &command,
                                            1, ORCM_SCD_CMD_T))) {
        orcm_octl_error("pack");
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }

    command = cmd;
    /* pack the sub-command flag */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &command,
                                            1, ORCM_SCD_CMD_T))) {
        orcm_octl_error("pack");
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }

    /* This is a session specific power setting, not global */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &per_session,
                                            1, OPAL_BOOL))) {
        orcm_octl_error("pack");
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }

    id = (orcm_alloc_id_t)session;
    /* Pack the session ID */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &id,
                                            1, ORCM_ALLOC_ID_T))) {
        orcm_octl_error("pack");
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }

    switch(cmd) {
    case ORCM_SET_POWER_BUDGET_COMMAND:
         int_param = (int)strtol(argv[4], NULL, 10);
        // FIXME: validate that power budget is reasonable

        /* pack the power budget */
        if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &int_param, 1, OPAL_INT32))) {
            orcm_octl_error("pack");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            return rc;
        }
        break;
    case ORCM_SET_POWER_MODE_COMMAND:
        if (isdigit(argv[4][strlen(argv[4]) - 1])) {
            int_param = (int)strtol(argv[4], NULL, 10);
        }
        else {
            int_param = orcm_pwrmgmt_get_mode_val(argv[4]);
        }

        if (0 > int_param || ORCM_PWRMGMT_NUM_MODES <= int_param ) {
            orcm_octl_error("illegal-pw-mode");
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return ORCM_ERR_BAD_PARAM;
        }
        // FIXME: validate that power mode is valid

        /* pack the power mode */
        if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &int_param, 1, OPAL_INT32))) {
            orcm_octl_error("pack");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }
        break;
    case ORCM_SET_POWER_WINDOW_COMMAND:
        int_param = (int)strtol(argv[4], NULL, 10);
        // FIXME: validate that power window is reasonable

        /* pack the power window */
        if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &int_param, 1, OPAL_INT32))) {
            orcm_octl_error("pack");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }
        break;
    case ORCM_SET_POWER_OVERAGE_COMMAND:
        int_param = (int)strtol(argv[4], NULL, 10);
        // FIXME: validate that power overage is reasonable

        /* pack the power overage */
        if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &int_param, 1, OPAL_INT32))) {
            orcm_octl_error("pack");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }
        break;
    case ORCM_SET_POWER_UNDERAGE_COMMAND:
        int_param = (int)strtol(argv[4], NULL, 10);
        // FIXME: validate that power underage is reasonable

        /* pack the power underage */
        if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &int_param, 1, OPAL_INT32))) {
            orcm_octl_error("pack");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }
        break;
    case ORCM_SET_POWER_OVERAGE_TIME_COMMAND:
        int_param = (int)strtol(argv[4], NULL, 10);
        // FIXME: validate that power overage time is reasonable

        /* pack the power overage time */
        if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &int_param, 1, OPAL_INT32))) {
            orcm_octl_error("pack");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }
        break;
    case ORCM_SET_POWER_UNDERAGE_TIME_COMMAND:
        int_param = (int)strtol(argv[4], NULL, 10);
        // FIXME: validate that power underage time is reasonable

        /* pack the power underage time */
        if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &int_param, 1, OPAL_INT32))) {
            orcm_octl_error("pack");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }
        break;
    case ORCM_SET_POWER_FREQUENCY_COMMAND:
        if (isdigit(argv[4][strlen(argv[4]) - 1])) {
            float_param = (float)strtof(argv[4], NULL);
        }
        else {
            if(!strncmp(argv[4], "MAX_FREQ", 8)) {
                float_param = ORCM_PWRMGMT_MAX_FREQ;
            }
            else if(!strncmp(argv[4], "MIN_FREQ", 8)) {
                float_param = ORCM_PWRMGMT_MIN_FREQ;
            }
            else {
                orcm_octl_usage("session-set", INVALID_USG);
                OBJ_RELEASE(buf);
                OBJ_DESTRUCT(&xfer);
                orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
                return ORCM_ERR_BAD_PARAM;
            }
        }
        // FIXME: validate that power freq is reasonable

        /* pack the power freq */
        if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &float_param, 1, OPAL_FLOAT))) {
            orcm_octl_error("pack");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }
        break;
    case ORCM_SET_POWER_STRICT_COMMAND:
        if (isdigit(argv[4][strlen(argv[4]) - 1])) {
            bool_param = (bool)strtol(argv[4], NULL, 10);
        }
        else {
            if(!strncmp(argv[4], "true", 4)) {
                bool_param = true;
            }
            else if(!strncmp(argv[4], "false", 5)) {
                bool_param = false;
            }
            else {
                orcm_octl_usage("session-set", INVALID_USG);
                OBJ_RELEASE(buf);
                OBJ_DESTRUCT(&xfer);
                orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
                return ORCM_ERR_BAD_PARAM;
            }
        }
        // FIXME: validate that power freq is reasonable

        /* pack the power freq */
        if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &bool_param, 1, OPAL_BOOL))) {
            orcm_octl_error("pack");
            OBJ_RELEASE(buf);
            OBJ_DESTRUCT(&xfer);
            orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
            return rc;
        }
        break;
    default:
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return ORTE_ERR_BAD_PARAM;
    }

    if (ORTE_SUCCESS != (rc = orte_rml.send_buffer_nb(ORTE_PROC_MY_SCHEDULER,
                                                      buf,
                                                      ORCM_RML_TAG_SCD,
                                                      orte_rml_send_callback,
                                                      NULL))) {
        orcm_octl_error("connection-fail");
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }
    /* get result */
    n=1;
    ORCM_WAIT_FOR_COMPLETION(xfer.active, ORCM_OCTL_WAIT_TIMEOUT, &rc);
    if (ORCM_SUCCESS != rc) {
        orcm_octl_error("connection-fail");
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &result,
                                              &n, OPAL_INT))) {
        orcm_octl_error("unpack");
        OBJ_DESTRUCT(&xfer);
        return rc;
    }
    if (0 == result) {
        orcm_octl_info("success");
    } else {
        orcm_octl_error("session-set");
    }

    return ORCM_SUCCESS;
}

int orcm_octl_session_get(int cmd, char **argv)
{
    orcm_scd_cmd_flag_t command;
    opal_buffer_t *buf;
    orte_rml_recv_cb_t xfer;
    int rc, n, success;
    int32_t int_param;
    float float_param;
    bool bool_param;
    long session;
    orcm_alloc_id_t id;
    bool per_session = true;

    if (4 != opal_argv_count(argv)) {
        orcm_octl_usage("session-get", INVALID_USG);
        return ORCM_ERR_BAD_PARAM;
    }

    session = strtol(argv[3], NULL, 10);

    /* setup to receive the result */
    OBJ_CONSTRUCT(&xfer, orte_rml_recv_cb_t);
    xfer.active = true;
    orte_rml.recv_buffer_nb(ORTE_NAME_WILDCARD,
                            ORCM_RML_TAG_SCD,
                            ORTE_RML_NON_PERSISTENT,
                            orte_rml_recv_callback, &xfer);

    /* send it to the scheduler */
    buf = OBJ_NEW(opal_buffer_t);

    command = ORCM_GET_POWER_COMMAND;
    /* pack the command flag */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &command,
                                            1, ORCM_SCD_CMD_T))) {
        orcm_octl_error("pack");
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }

    command = cmd;
    /* pack the sub-command flag */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &command,
                                            1, ORCM_SCD_CMD_T))) {
        orcm_octl_error("pack");
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }

    /* This is a session specific power setting, not global */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &per_session,
                                            1, OPAL_BOOL))) {
        orcm_octl_error("pack");
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }

    id = (orcm_alloc_id_t)session;
    /* Pack the session ID */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(buf, &id,
                                            1, ORCM_ALLOC_ID_T))) {
        orcm_octl_error("pack");
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }

    if (ORTE_SUCCESS != (rc = orte_rml.send_buffer_nb(ORTE_PROC_MY_SCHEDULER,
                                                      buf,
                                                      ORCM_RML_TAG_SCD,
                                                      orte_rml_send_callback,
                                                      NULL))) {
        orcm_octl_error("connection-fail");
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }

    /* get result */
    n=1;
    ORCM_WAIT_FOR_COMPLETION(xfer.active, ORCM_OCTL_WAIT_TIMEOUT, &rc);
    if (ORCM_SUCCESS != rc) {
        orcm_octl_error("connection-fail");
        OBJ_DESTRUCT(&xfer);
        orte_rml.recv_cancel(ORTE_NAME_WILDCARD, ORCM_RML_TAG_SCD);
        return rc;
    }

    if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &success,
                                              &n, OPAL_INT))) {
        orcm_octl_error("unpack");
        OBJ_DESTRUCT(&xfer);
        return rc;
    }

    if(OPAL_SUCCESS != success) {
        OBJ_DESTRUCT(&xfer);
        return success;
    }

    switch(cmd) {
    case ORCM_GET_POWER_BUDGET_COMMAND:
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &int_param,
                                                  &n, OPAL_INT32))) {
            orcm_octl_error("unpack");
            OBJ_DESTRUCT(&xfer);
            return rc;
        }
        orcm_octl_info("session-budget", session, int_param);
    break;
    case ORCM_GET_POWER_MODE_COMMAND:
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &int_param,
                                                  &n, OPAL_INT32))) {
            orcm_octl_error("unpack");
            OBJ_DESTRUCT(&xfer);
            return rc;
        }
        orcm_octl_info("session-mode", session, orcm_pwrmgmt_get_mode_string(int_param));
    break;
    case ORCM_GET_POWER_WINDOW_COMMAND:
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &int_param,
                                                  &n, OPAL_INT32))) {
            orcm_octl_error("unpack");
            OBJ_DESTRUCT(&xfer);
            return rc;
        }
        orcm_octl_info("session-window", session, int_param);
    break;
    case ORCM_GET_POWER_OVERAGE_COMMAND:
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &int_param,
                                                  &n, OPAL_INT32))) {
            orcm_octl_error("unpack");
            OBJ_DESTRUCT(&xfer);
            return rc;
        }
        orcm_octl_info("session-budget-limit", session, "overage", int_param);
    break;
    case ORCM_GET_POWER_UNDERAGE_COMMAND:
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &int_param,
                                                  &n, OPAL_INT32))) {
            orcm_octl_error("unpack");
            OBJ_DESTRUCT(&xfer);
            return rc;
        }
        orcm_octl_info("session-budget-limit", session, "'underage'", int_param);
    break;
    case ORCM_GET_POWER_OVERAGE_TIME_COMMAND:
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &int_param,
                                                  &n, OPAL_INT32))) {
            orcm_octl_error("unpack");
            OBJ_DESTRUCT(&xfer);
            return rc;
        }
        orcm_octl_info("session-time-limit", session, "overage", int_param);
    break;
    case ORCM_GET_POWER_UNDERAGE_TIME_COMMAND:
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &int_param,
                                                  &n, OPAL_INT32))) {
            orcm_octl_error("unpack");
            OBJ_DESTRUCT(&xfer);
            return rc;
        }
        orcm_octl_info("session-time-limit", session, "'underage'", int_param);
    break;
    case ORCM_GET_POWER_FREQUENCY_COMMAND:
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &float_param,
                                                  &n, OPAL_FLOAT))) {
            orcm_octl_error("unpack");
            OBJ_DESTRUCT(&xfer);
            return rc;
        }
        if(fabs((float)(float_param - (float)ORCM_PWRMGMT_MAX_FREQ)) < 0.0001) {
            orcm_octl_info("session-freq-df", session, "MAX_FREQ");
        }
        else if(fabs((float)(float_param - ORCM_PWRMGMT_MIN_FREQ)) < 0.0001) {
            orcm_octl_info("session-freq", session, "MIN_FREQ");
        }
        else {
            orcm_octl_info("session-freq-GHz", session, float_param);
        }
    break;
    case ORCM_GET_POWER_STRICT_COMMAND:
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(&xfer.data, &bool_param,
                                                  &n, OPAL_BOOL))) {
            orcm_octl_error("unpack");
            OBJ_DESTRUCT(&xfer);
            return rc;
        }
        orcm_octl_info("session-freq-strict", session, bool_param ? "true" : "false");
    break;
    default:
        OBJ_RELEASE(buf);
        OBJ_DESTRUCT(&xfer);
        return ORTE_ERR_BAD_PARAM;
    }

    return ORCM_SUCCESS;
}

