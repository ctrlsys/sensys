dnl -*- shell-script -*-
dnl
dnl Copyright (c) 2014      Intel Corporation. All rights reserved.
dnl Copyright (c) 2016      Intel Corporation. All rights reserved.
dnl $COPYRIGHT$
dnl 
dnl Additional copyrights may follow
dnl 
dnl $HEADER$
dnl

# MCA_sensor_freq_CONFIG([action-if-found], [action-if-not-found])
# -----------------------------------------------------------
AC_DEFUN([MCA_orcm_sensor_freq_CONFIG], [
    AC_CONFIG_FILES([orcm/mca/sensor/freq/Makefile])

    AC_ARG_WITH([freq],
                [AC_HELP_STRING([--with-freq],
                                [Build freq support (default: no)])],
	                        [], with_freq=no)

    # do not build if support not requested
    AS_IF([test "$with_freq" != "no"],
          [AS_IF([test "$opal_found_linux" = "yes"],
                 [$1],
                 [AC_MSG_WARN([Core frequency sensing was requested but is only supported on Linux systems])
                  AC_MSG_ERROR([Cannot continue])
                  $2])
          ],
          [$2])
])dnl
