#
# Copyright (c) 2014      Intel Corporation.  All rights reserved. 
#
# Copyright (c) 2016      Intel Corporation. All rights reserved.
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#

sources = \
        cfgi_file10.h \
        cfgi_file10.c \
        cfgi_file10_component.c

# Make the output library in this directory, and name it either
# mca_<project>_<type>_<name>.la (for DSO builds) or
# libmca_<project>_<type>_<name>.la (for static builds).

if MCA_BUILD_orcm_cfgi_file10_DSO
lib =
lib_sources =
component = mca_cfgi_file10.la
component_sources = $(sources)
else
lib = libmca_cfgi_file10.la
lib_sources = $(sources)
component =
ponent_sources =
endif

mcacomponentdir = $(orcmlibdir)
mcacomponent_LTLIBRARIES = $(component)
mca_cfgi_file10_la_SOURCES = $(component_sources)
mca_cfgi_file10_la_LDFLAGS = -module -avoid-version

noinst_LTLIBRARIES = $(lib)
libmca_cfgi_file10_la_SOURCES = $(lib_sources)
libmca_cfgi_file10_la_LDFLAGS = -module -avoid-version
