# -*- makefile -*-
#
# Copyright (c) 2013-2017 Intel Corporation. All rights reserved.
## $COPYRIGHT$
#
# Additional copyrights may follow
#
# $HEADER$
#

# This makefile.am does not stand on its own - it is included from orcm/Makefile.am

headers += \
        util/utils.h \
        util/cli.h \
        util/attr.h \
        util/logical_group.h \
        util/vardata.h \
        util/string_utils.h \
        util/dlopen_helper.h \
        util/led_control/led_control.h \
        util/led_control/led_control_interface.h \
        util/led_control/ipmicmd_wrapper.h

liborcm_la_SOURCES += \
        util/error_strings.c \
        util/utils.c \
        util/cli.c \
        util/attr.c \
        util/logical_group.c \
        util/workflow_util.c \
        util/vardata.cpp \
        util/dlopen_helper.cpp \
        util/led_control/led_control.cpp \
        util/led_control/led_control_interface.cpp \
        util/led_control/ipmicmd_wrapper.cpp

if HAVE_IPMI
CPPFLAGS += -DHAVE_LED_CONTROL_SUPPORT
endif
