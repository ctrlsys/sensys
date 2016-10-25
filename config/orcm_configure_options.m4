dnl -*- shell-script -*-
dnl
dnl Copyright (c) 2013-2016 Intel Corporation.  All rights reserved.
dnl
dnl Copyright (c) 2016      Intel Corporation. All rights reserved.
dnl $COPYRIGHT$
dnl
dnl Additional copyrights may follow
dnl
dnl $HEADER$
dnl


AC_DEFUN([ORCM_CONFIGURE_OPTIONS],[
opal_show_subtitle "ORCM Configuration options"
])dnl

AC_DEFUN([ORCM_LOAD_CONFIGURATION],[
    if test "$with_platform" != "" ; then
        AC_MSG_CHECKING([for config file])
        # define an ORCM site configuration filename
        platform_site_config_file="`basename $platform_loaded`.xml"
        # look where platform file is located for platform.xml name
        if test -r "${platform_file_dir}/${platform_site_config_file}" ; then
            AC_SUBST(OPAL_SITE_CONFIG_FILE, [$platform_file_dir/$platform_site_config_file])
            AC_MSG_RESULT([$platform_file_dir/$platform_site_config_file])
            AC_SUBST(OPAL_SITE_CONFIG_FILE_FOUND, "yes")
        else
            AC_MSG_RESULT([no])
            AC_SUBST(OPAL_SITE_CONFIG_FILE_FOUND, "no")
        fi
    fi
    if test "$with_syslog" = "yes"; then
        AC_MSG_CHECKING([for rsyslog support config file])
        rsyslog_config_file="10-rsyslog_orcm.conf"
        if test -r "${platform_file_dir}/${rsyslog_config_file}" ; then
            AC_SUBST(OPAL_SYSLOG_CONFIG_FILE, [$platform_file_dir/$rsyslog_config_file])
            AC_MSG_RESULT([$platform_file_dir/$rsyslog_config_file])
            AC_SUBST(OPAL_SYSLOG_CONFIG_FILE_FOUND, "yes")
        else
            AC_MSG_RESULT([no])
            AC_SUBST(OPAL_SYSLOG_CONFIG_FILE_FOUND, "no")
        fi
    fi
])dnl
