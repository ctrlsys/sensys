/*
 * Copyright (c) 2014      Intel, Inc.  All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "orcm/tools/ocli/common.h"
#include "orcm/tools/ocli/ocli.h"

/******************
 * Local Functions
 ******************/
static int orcm_ocli_init(int argc, char *argv[]);
static int run_cmd(char *cmd);

/*****************************************
 * Global Vars for Command line Arguments
 *****************************************/
typedef struct {
    bool help;
    bool version;
    bool verbose;
    int output;
} orcm_ocli_globals_t;

orcm_ocli_globals_t orcm_ocli_globals;

opal_cmd_line_init_t cmd_line_opts[] = {
    { NULL,
      'h', NULL, "help",
      0,
      &orcm_ocli_globals.help, OPAL_CMD_LINE_TYPE_BOOL,
      "This help message" },

    { NULL,
      'v', NULL, "verbose",
      0,
      &orcm_ocli_globals.verbose, OPAL_CMD_LINE_TYPE_BOOL,
      "Be Verbose" },

    { NULL,
        'V', NULL, "version",
        0,
        &orcm_ocli_globals.version, OPAL_CMD_LINE_TYPE_BOOL,
        "Show version information" },

    /* End of list */
    { NULL, '\0', NULL, NULL, 0, NULL, OPAL_CMD_LINE_TYPE_NULL, NULL }
};

int
main(int argc, char *argv[])
{
    int ret, rc;

    /* initialize, parse command line, and setup frameworks */
    ret = orcm_ocli_init(argc, argv);

    if (ORTE_SUCCESS != (rc = orcm_finalize())) {
        fprintf(stderr, "Failed orcm_finalize\n");
        exit(rc);
    }

    return ret;
}

static int orcm_ocli_init(int argc, char *argv[])
{
    opal_cmd_line_t cmd_line;
    orcm_ocli_globals_t tmp = { false,    /* help */
        false,    /* version */
        false,    /* verbose */
        -1};      /* output */
    
    orcm_ocli_globals = tmp;
    char *args = NULL;
    char *str = NULL;
    orcm_cli_t cli;
    orcm_cli_cmd_t *cmd = NULL;
    int tailc;
    char **tailv = NULL;
    int ret;
    char *mycmd = NULL;

    /* Make sure to init util before parse_args
     * to ensure installdirs is setup properly
     * before calling mca_base_open(); */
    if( OPAL_SUCCESS != (ret = opal_init_util(&argc, &argv)) ) {
        return ret;
    }

    /* Parse the command line options */
    opal_cmd_line_create(&cmd_line, cmd_line_opts);
    
    mca_base_cmd_line_setup(&cmd_line);

    opal_cmd_line_parse(&cmd_line, true, argc, argv);
    
    /* Setup OPAL Output handle from the verbose argument */
    if( orcm_ocli_globals.verbose ) {
        orcm_ocli_globals.output = opal_output_open(NULL);
        opal_output_set_verbosity(orcm_ocli_globals.output, 10);
    } else {
        orcm_ocli_globals.output = 0; /* Default=STDERR */
    }

    if (orcm_ocli_globals.help) {
        args = opal_cmd_line_get_usage_msg(&cmd_line);
        str = opal_show_help_string("help-ocli.txt", "usage", false, args);
        if (NULL != str) {
            printf("%s", str);
            free(str);
        }
        free(args);
        /* If we show the help message, that should be all we do */
        exit(0);
    }
    
    if (orcm_ocli_globals.version) {
        str = opal_show_help_string("help-ocli.txt", "version", false,
                                    ORCM_VERSION,
                                    PACKAGE_BUGREPORT);
        if (NULL != str) {
            printf("%s", str);
            free(str);
        }
        exit(0);
    }

    /* Since this process can now handle MCA/GMCA parameters, make sure to
     * process them. */
    if (OPAL_SUCCESS != (ret = mca_base_cmd_line_process_args(&cmd_line, &environ, &environ))) {
        exit(ret);
    }

    /* get the commandline without mca params */
    opal_cmd_line_get_tail(&cmd_line, &tailc, &tailv);

    /* initialize orcm for use as a tool */
    if (ORCM_SUCCESS != (ret = orcm_init(ORCM_TOOL))) {
        fprintf(stderr, "Failed to initialize\n");
        if (NULL != tailv) {
            free(tailv);
        }
        exit(ret);
    }

    if (0 == tailc) {
        /* if the user hasn't specified any commands,
         * run interactive cli to help build it */
        OBJ_CONSTRUCT(&cli, orcm_cli_t);
        orcm_cli_create(&cli, cli_init);
        /* give help on top level commands */
        printf("*** WELCOME TO OCLI ***\n Possible commands:\n");
        OPAL_LIST_FOREACH(cmd, &cli.cmds, orcm_cli_cmd_t) {
            orcm_cli_print_cmd(cmd, NULL);
        }
        mycmd = NULL;
        /* run interactive cli */
        orcm_cli_get_cmd("ocli", &cli, &mycmd);
        if (!mycmd) {
            fprintf(stderr, "\nNo command specified\n");
            return ORCM_ERROR;
        }
    } else {
        /* otherwise use the user specified command */
        mycmd = (opal_argv_join(tailv, ' '));
    }

    ret = run_cmd(mycmd);
    free(mycmd);
    opal_argv_free(tailv);

    return ret;
}

static int ocli_command_to_int(char *command)
{
    /* this is used for nicer looking switch statements
     * just convert a string to the location of the string
     * in the pre-defined array found in ocli.h*/
    int i;
    
    if (!command) {
        return -1;
    }
    
    for (i = 0; i < opal_argv_count((char **)orcm_ocli_commands); i++) {
        if (0 == strcmp(command, orcm_ocli_commands[i])) {
            return i;
        }
    }
    return -1;
}

static int run_cmd(char *cmd) {
    char **cmdlist = NULL;
    char *fullcmd = NULL;
    int rc;
    
    cmdlist = opal_argv_split(cmd, ' ');
    if (0 == opal_argv_count(cmdlist)) {
        fprintf(stderr, "No command parsed\n");
        opal_argv_free(cmdlist);
        return ORCM_ERROR;
    }
    
    rc = ocli_command_to_int(cmdlist[0]);
    if (ORCM_ERROR == rc) {
        fullcmd = opal_argv_join(cmdlist, ' ');
        fprintf(stderr, "Unknown command: %s\n", fullcmd);
        free(fullcmd);
        opal_argv_free(cmdlist);
        return ORCM_ERROR;
    }
    
    /* call corresponding function to passed command */
    switch (rc) {
    case 0: //resource
        rc = ocli_command_to_int(cmdlist[1]);
        if (-1 == rc) {
            fullcmd = opal_argv_join(cmdlist, ' ');
            fprintf(stderr, "Unknown command: %s\n", fullcmd);
            free(fullcmd);
            rc = ORCM_ERROR;
            break;
        }
            
        switch (rc) {
        case 3: //status
            if (ORCM_SUCCESS != (rc = orcm_ocli_resource_status(cmdlist))) {
                ORTE_ERROR_LOG(rc);
            }
            break;
        case 4: //availability
            if (ORCM_SUCCESS != (rc = orcm_ocli_resource_availability(cmdlist))) {
                ORTE_ERROR_LOG(rc);
            }
            break;
        default:
            fullcmd = opal_argv_join(cmdlist, ' ');
            fprintf(stderr, "Illegal command: %s\n", fullcmd);
            free(fullcmd);
            break;
        }
        break;
    case 1: // queue
        rc = ocli_command_to_int(cmdlist[1]);
        if (-1 == rc) {
            fullcmd = opal_argv_join(cmdlist, ' ');
            fprintf(stderr, "Unknown command: %s\n", fullcmd);
            free(fullcmd);
            rc = ORCM_ERROR;
            break;
        }
            
        switch (rc) {
        case 3: //status
            if (ORCM_SUCCESS != (rc = orcm_ocli_queue_status(cmdlist))) {
                ORTE_ERROR_LOG(rc);
            }
            break;
        case 5: //policy
            if (ORCM_SUCCESS != (rc = orcm_ocli_queue_policy(cmdlist))) {
                ORTE_ERROR_LOG(rc);
            }
            break;
        default:
            fullcmd = opal_argv_join(cmdlist, ' ');
            fprintf(stderr, "Illegal command: %s\n", fullcmd);
            free(fullcmd);
            break;
        }
        break;
    case 2: // session
        rc = ocli_command_to_int(cmdlist[1]);
        if (-1 == rc) {
            fullcmd = opal_argv_join(cmdlist, ' ');
            fprintf(stderr, "Unknown command: %s\n", fullcmd);
            free(fullcmd);
            rc = ORCM_ERROR;
            break;
        }
            
        switch (rc) {
        case 3: //status
            if (ORCM_SUCCESS != (rc = orcm_ocli_session_status(cmdlist))) {
                ORTE_ERROR_LOG(rc);
            }
            break;
        case 6: //cancel
            if (ORCM_SUCCESS != (rc = orcm_ocli_session_cancel(cmdlist))) {
                ORTE_ERROR_LOG(rc);
            }
            break;
        default:
            fullcmd = opal_argv_join(cmdlist, ' ');
            fprintf(stderr, "Illegal command: %s\n", fullcmd);
            free(fullcmd);
            break;
        }
        break;
    default:
        fullcmd = opal_argv_join(cmdlist, ' ');
        fprintf(stderr, "Illegal command: %s\n", fullcmd);
        free(fullcmd);
        rc = ORCM_ERROR;
        break;
    }
    
    opal_argv_free(cmdlist);
    return rc;
}
