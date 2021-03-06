/*
 * Copyright (c) 2016      Intel Corporation. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <regex.h>

#include "orcm/util/utils.h"
#include "orcm/tools/octl/common.h"

#include "opal/mca/installdirs/installdirs.h"

#define MAX_SIZE 1024

char *search_msg(regex_t regex_label);
int regex_label( regex_t *regex, char *type, char *label);
char *get_usage_label_error(int error);

const char *next_label = "^[[:space:]]*\\[[^:]+[^]]+\\][[:space:]]*$";

/**
 * @brief Function that takes a string label and finds the message
 *        related to the label in the "messages.txt" file.
 *
 * @param regex_label Pre-compiled message label pattern.
 *
 * @return char* Message or NULL if the message wasn't found.
 *               DON'T FORGET TO FREE!
 */
char *search_msg(regex_t regex_label) {
    FILE *file = NULL;
    char line[MAX_SIZE] = {0,};
    int regex_res = 0;
    char *msg = NULL;
    char *datadir = NULL;

    regex_t regex;

    asprintf(&datadir,"%s%s", opal_install_dirs.opaldatadir,"/help-octl.txt");
    if(NULL != datadir) {

        regex_res = regcomp(&regex, next_label, REG_EXTENDED);
        file = fopen(datadir, "r");

        if (NULL !=file) {
            if(!regex_res) {
               while (NULL != fgets(line, MAX_SIZE, file)) {
                   regex_res = regexec(&regex_label, line, 0, NULL, 0);
                   if (!regex_res){
                       msg = strdup("\0");
                       while (NULL != fgets(line, 1024, file) && NULL != msg) {
                           if (!regexec(&regex, line, 0, NULL, 0)) {
                                // remove unnecessary last "\n"
                                if (msg[strlen(msg)-1] == '\n') {
                                    msg[strlen(msg)-1] = '\0';
                                }
                                break;
                           } else {
                               msg = (char *) realloc(msg, strlen(msg) + strlen(line) + 1);
                               if (NULL != msg){
                                   strcat(msg, line);
                              }
                          }
                       }
                       break;
                   }
               }
               regfree(&regex);
            }
            fclose(file);
        }
        SAFEFREE(datadir);
    }
    return msg;
}

/**
 * @brief Function that creates and compiles a regular expresion into a form
 *        that is used to find a message label.
 *
 * @param regex Pointer to a label pattern buffer.
 *
 * @param type  Type of message.
 *
 * @param label Defined message label.
 *
 * @return int Returns 0 for a succeful compilation or an error code for a
 *             failure.
 */


int regex_label( regex_t *regex, char *type, char *label) {
    char *new_label = NULL;
    char *regex_exp = "^\\[%s\\:%s\\]";
    int rc = 0;

    rc = asprintf(&new_label, regex_exp, type, label);
    if (NULL != new_label){
        rc = regcomp(regex, new_label, REG_EXTENDED);
        SAFEFREE(new_label);
    }
    return rc;
}

/**
 * @brief Function to get error message label from a usage error.
 *
 * @param error Type of command line usage error.
 *
 * @return char* Returns an error message label or NULL for a failure.
 *               DON'T FORGET TO FREE!
 *
 */
char *get_usage_label_error(int error) {
    char *err_label = NULL;

    switch(error) {
        case 0 :
           err_label = strdup("invalid-argument");
           break;
        case 1 :
           err_label = strdup("illegal-cmd");
           break;
        default :
           err_label = strdup("unknown-usg");
           break;
    }
    return err_label;
}

/**
 * @brief Function to show revelant information regarding the operation
 *        executed on octl, this will print the message to standard output.
 *
 * @param label Defined message label.
 */
void orcm_octl_info(char *label, ...){
    static char *info_msg = NULL;
    regex_t r_label;

    va_list var;
    va_start (var,label);

    if (!regex_label(&r_label, "info", label)) {
        info_msg = search_msg(r_label);
        if (NULL != info_msg) {
            vfprintf(stdout, info_msg, var);
        }
        regfree(&r_label);
    }

    va_end(var);
    SAFEFREE(info_msg);
}

/**
 * @brief For error messages on octl operations, this will print a
 *        pre-defined message to stderr.
 *
 * @param label Defined message label.
 */
void orcm_octl_error(char *label, ...){
    static char *error_msg = NULL;
    const char *error_prepend = "ERROR: ";
    char output[MAX_SIZE];
    regex_t r_label;

    memset(output, 0, MAX_SIZE);

    va_list var;
    va_start(var, label);

    if (!regex_label( &r_label,"error", label)) {
        error_msg = search_msg(r_label);

        if (NULL != error_msg) {
            snprintf(output, MAX_SIZE, "%s%s", error_prepend, error_msg);
            vfprintf(stderr, output, var);
            SAFEFREE(error_msg);
        } else {
            fprintf(stderr,"ERROR: Unkown error.");
        }
        regfree(&r_label);
    } else {
      fprintf(stderr,"ERROR: Unable to generate message.");
    }

   va_end(var);
}

/**
 * @brief Is for error messages in the command line usage,
 *        prints a invalid-argument error with a pre-defined
 *        usage message.
 *
 * @param label Defined usage message label.
 *
 * @param error Type of command line usage error.
 */
void orcm_octl_usage(char *label, int error){
    char *usage_msg = NULL;
    char *output = NULL;
    char *err_label = NULL;

    regex_t r_label;

    if (!regex_label( &r_label,"usage", label)) {
        usage_msg = search_msg(r_label);

        if (NULL != usage_msg) {
            // remove unnecessary last "\n"
            if (usage_msg[strlen(usage_msg)-1] == '\n')
                usage_msg[strlen(usage_msg)-1] = '\0';

            asprintf(&output,"USAGE: %s" ,usage_msg);
            err_label = get_usage_label_error(error);

            if (NULL != output || NULL != err_label)
                orcm_octl_error(err_label, output);
        }
        regfree(&r_label);
    }

   SAFEFREE(output);
   SAFEFREE(usage_msg);
   SAFEFREE(err_label);
}
