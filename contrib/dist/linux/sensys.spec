define make_spec

#
# Global definitions
#
%{!?sensys_name: %define sensys_name $(name)}
%{!?sensys_version: %define sensys_version $(version)}
%{!?sensys_package_version: %define sensys_package_version default}
%{!?sensys_release: %define sensys_release $(release)}
%{!?static_build: %define static_build $(is_static)}
%{!?sensys_build_root: %define sensys_build_root %{_tmppath}/%{sensys_name}-%{sensys_version}-%{sensys_release}-root}

#
# Configure script options
#

%{!?config_platform: %define config_platform --with-platform=contrib/platform/intel/hillsboro/orcm-linux}

%if 0%{?suse_version} > 1220
%{!?config_postgres: %define config_postgres --with-postgres=%{_usr}/lib/postgresql94}
%else
%{!?config_postgres: %define config_postgres --with-postgres=%{_usr}/pgsql-9.3}
%endif

%{!?config_extras: %define config_extras %{nil}}
%{!?build_mode: %define build_mode --enable-debug=no}

%{!?configure_flags: %define configure_flags %{config_platform} %{config_postgres} %{config_extras} %{build_mode}}


%define _prefix /opt/%{sensys_name}
%define _libdir %{_prefix}/lib
%define _sysconfdir %{_prefix}/etc
%define _datarootdir %{_prefix}/share
%define _unpackaged_files_terminate_build 0

Summary: A cluster monitoring system architected for exascale systems.
Name: %{sensys_name}
Version: %{sensys_version}
Release: %{sensys_release}
License: BSD-3-Clause
Group: System Environment/Base
URL: https://github.com/intel-ctrlsys/sensys
Source0: %{name}-%{version}.tar.gz
Prefix: %{_prefix}

%{?systemd_requires}
BuildRequires:  pkgconfig(systemd)
BuildRequires: flex
BuildRequires: gcc-c++
BuildRequires: ipmiutil-devel >= 2.9.6
BuildRequires: libtool
BuildRequires: sigar-devel >= 1.6.4
BuildRequires: postgresql93-devel
BuildRequires: net-snmp-devel >= 5.7.2
BuildRequires: zeromq-devel >= 4.0.4
BuildRequires: munge-devel
BuildRequires: libesmtp-devel
BuildRequires: openssl-devel
BuildRequires: unixODBC-devel
BuildRequires: libcurl-devel

%if ! %{static_build}
Requires: %{sensys_name}-common
%endif
Requires: ipmiutil >= 2.9.6
Requires: sigar >= 1.6.4
Requires: net-snmp >= 5.7.2
%if 0%{?suse_version} > 1200
Requires: postgresql94
Requires: libzmq3
Requires: libcurl4
%else
Requires: postgresql93
Requires: zeromq >= 4.0.4
Requires: libcurl
%endif
Requires: unixODBC
Requires: libesmtp
Requires: munge
Requires: openssl
Requires: python
Requires: python-psycopg2
Requires: python-sqlalchemy

%if 0%{?suse_version} > 1200
PreReq: %fillup_prereq
%endif

%description
Sensys provides resilient and scalable monitoring for resource utilization
and node state of health, collecting all the data in a database for subsequent
analysis. Sensys includes several loadable plugins that monitor various metrics
related to different features present in each node like temperature, voltage,
power usage, memory, disk and process information.

%if ! %{static_build}
%package devel
Summary: Development files for Sensys
Group: Development/Libraries
Requires: %{sensys_name}
%description devel
The headers needed to develop new components for Sensys

%package common
Summary: Configuration files for Sensys
Group: System Environment/Base
%description common
Scripts for daemon start process on boot process.
%endif

%prep
%setup -q

%build
%configure %{configure_flags}
%{__make} %{?_smp_mflags}

%install
%{__make} %{?_smp_mflags} DESTDIR=%{buildroot} install

%if 0%{?suse_version} > 1200
install -D -m 0644 contrib/dist/linux/orcmd.sysconfig %{buildroot}/var/adm/fillup-templates/sysconfig.orcmd
install -D -m 0644 contrib/dist/linux/orcmsched.sysconfig %{buildroot}/var/adm/fillup-templates/sysconfig.orcmsched
%else
install -D -m 0644 contrib/dist/linux/orcmd.sysconfig %{buildroot}/etc/sysconfig/orcmd
install -D -m 0644 contrib/dist/linux/orcmsched.sysconfig %{buildroot}/etc/sysconfig/orcmsched
%endif

install -D -m 0644 contrib/database/sensys-schema.sql %{buildroot}%{_datarootdir}/db-schema/sensys-schema.sql

cd %{buildroot}
chmod -R +r *

# File list for static compilation
%if %{static_build}
find -L -type f | sed -e s@^\.@@ > %{_sourcedir}/files.txt

%clean

%files -f %{_sourcedir}/files.txt
%defattr(-,root,root,-)

# Start of filelist definition for dynamic compilation
%else

%clean

%files
%defattr(-,root,root,-)
%dir %{_prefix}
%dir %{_prefix}/bin
%dir %{_prefix}/etc
%dir %{_prefix}/lib
%dir %{_prefix}/lib/openmpi
%dir %{_prefix}/share
%dir %{_prefix}/share/openmpi
%dir %{_prefix}/share/openmpi/amca-param-sets
%dir %{_prefix}/share/man
%dir %{_prefix}/share/man/man7
%dir %{_prefix}/share/man/man1
%dir %{_prefix}/share/db-schema
%{_prefix}/bin/opal_wrapper
%{_prefix}/bin/opalcc
%{_prefix}/bin/opalc++
%{_prefix}/bin/octl
%{_prefix}/bin/orcm-info
%{_prefix}/bin/orcmd
%{_prefix}/bin/orcmsched
%{_prefix}/bin/orcmcc
%{_prefix}/etc/openmpi-mca-params.conf
%{_prefix}/etc/orcm-site.xml
%{_prefix}/etc/10-rsyslog_orcm.conf
%{_prefix}/lib/liborcmopen-pal.so.0.0.0
%{_prefix}/lib/liborcmopen-pal.so.0
%{_prefix}/lib/liborcmopen-pal.so
%{_prefix}/lib/liborcmopen-pal.la
%{_prefix}/lib/openmpi/mca_dstore_hash.so
%{_prefix}/lib/openmpi/mca_dstore_hash.la
%{_prefix}/lib/openmpi/mca_pmix_native.so
%{_prefix}/lib/openmpi/mca_pmix_native.la
%{_prefix}/lib/openmpi/mca_pstat_linux.so
%{_prefix}/lib/openmpi/mca_pstat_linux.la
%{_prefix}/lib/openmpi/mca_sec_basic.so
%{_prefix}/lib/openmpi/mca_sec_basic.la
%{_prefix}/lib/openmpi/mca_errmgr_orcm.so
%{_prefix}/lib/openmpi/mca_errmgr_orcm.la
%{_prefix}/lib/openmpi/mca_errmgr_default_tool.so
%{_prefix}/lib/openmpi/mca_errmgr_default_tool.la
%{_prefix}/lib/openmpi/mca_ess_tool.so
%{_prefix}/lib/openmpi/mca_ess_tool.la
%{_prefix}/lib/openmpi/mca_ess_orcm.so
%{_prefix}/lib/openmpi/mca_ess_orcm.la
%{_prefix}/lib/openmpi/mca_notifier_syslog.so
%{_prefix}/lib/openmpi/mca_notifier_syslog.la
%{_prefix}/lib/openmpi/mca_oob_usock.so
%{_prefix}/lib/openmpi/mca_oob_usock.la
%{_prefix}/lib/openmpi/mca_oob_tcp.so
%{_prefix}/lib/openmpi/mca_oob_tcp.la
%{_prefix}/lib/openmpi/mca_ras_orcm.so
%{_prefix}/lib/openmpi/mca_ras_orcm.la
%{_prefix}/lib/openmpi/mca_ras_simulator.so
%{_prefix}/lib/openmpi/mca_ras_simulator.la
%{_prefix}/lib/openmpi/mca_ras_loadleveler.so
%{_prefix}/lib/openmpi/mca_ras_loadleveler.la
%{_prefix}/lib/openmpi/mca_rmaps_ppr.so
%{_prefix}/lib/openmpi/mca_rmaps_ppr.la
%{_prefix}/lib/openmpi/mca_rml_oob.so
%{_prefix}/lib/openmpi/mca_rml_oob.la
%{_prefix}/lib/openmpi/mca_routed_binomial.so
%{_prefix}/lib/openmpi/mca_routed_binomial.la
%{_prefix}/lib/openmpi/mca_routed_direct.so
%{_prefix}/lib/openmpi/mca_routed_direct.la
%{_prefix}/lib/openmpi/mca_routed_debruijn.so
%{_prefix}/lib/openmpi/mca_routed_debruijn.la
%{_prefix}/lib/openmpi/mca_routed_radix.so
%{_prefix}/lib/openmpi/mca_routed_radix.la
%{_prefix}/lib/openmpi/mca_routed_orcm.so
%{_prefix}/lib/openmpi/mca_routed_orcm.la
%{_prefix}/lib/openmpi/mca_state_tool.so
%{_prefix}/lib/openmpi/mca_state_tool.la
%{_prefix}/lib/openmpi/mca_state_orcm.so
%{_prefix}/lib/openmpi/mca_state_orcm.la
%{_prefix}/lib/openmpi/mca_notifier_smtp.la
%{_prefix}/lib/openmpi/mca_notifier_smtp.so
%{_prefix}/lib/openmpi/analytics_extension_average.so
%{_prefix}/lib/openmpi/analytics_extension_average.la
%{_prefix}/lib/openmpi/mca_analytics_filter.so
%{_prefix}/lib/openmpi/mca_analytics_filter.la
%{_prefix}/lib/openmpi/mca_analytics_cott.so
%{_prefix}/lib/openmpi/mca_analytics_cott.la
%{_prefix}/lib/openmpi/mca_analytics_spatial.so
%{_prefix}/lib/openmpi/mca_analytics_spatial.la
%{_prefix}/lib/openmpi/mca_analytics_window.so
%{_prefix}/lib/openmpi/mca_analytics_window.la
%{_prefix}/lib/openmpi/mca_analytics_aggregate.so
%{_prefix}/lib/openmpi/mca_analytics_aggregate.la
%{_prefix}/lib/openmpi/mca_analytics_extension.so
%{_prefix}/lib/openmpi/mca_analytics_extension.la
%{_prefix}/lib/openmpi/mca_analytics_genex.so
%{_prefix}/lib/openmpi/mca_analytics_genex.la
%{_prefix}/lib/openmpi/mca_analytics_threshold.so
%{_prefix}/lib/openmpi/mca_analytics_threshold.la
%{_prefix}/lib/openmpi/mca_cfgi_file10.so
%{_prefix}/lib/openmpi/mca_cfgi_file10.la
%{_prefix}/lib/openmpi/mca_cfgi_file30.so
%{_prefix}/lib/openmpi/mca_cfgi_file30.la
%{_prefix}/lib/openmpi/mca_db_print.so
%{_prefix}/lib/openmpi/mca_db_print.la
%{_prefix}/lib/openmpi/mca_db_zeromq.so
%{_prefix}/lib/openmpi/mca_db_zeromq.la
%{_prefix}/lib/openmpi/mca_db_odbc.so
%{_prefix}/lib/openmpi/mca_db_odbc.la
%{_prefix}/lib/openmpi/mca_db_postgres.so
%{_prefix}/lib/openmpi/mca_db_postgres.la
%{_prefix}/lib/openmpi/mca_diag_memtest.so
%{_prefix}/lib/openmpi/mca_diag_memtest.la
%{_prefix}/lib/openmpi/mca_diag_ethtest.so
%{_prefix}/lib/openmpi/mca_diag_ethtest.la
%{_prefix}/lib/openmpi/mca_diag_cputest.so
%{_prefix}/lib/openmpi/mca_diag_cputest.la
%{_prefix}/lib/openmpi/mca_dispatch_dfg.so
%{_prefix}/lib/openmpi/mca_dispatch_dfg.la
%{_prefix}/lib/openmpi/mca_parser_pugi.so
%{_prefix}/lib/openmpi/mca_parser_pugi.la
%{_prefix}/lib/openmpi/mca_scd_pmf.so
%{_prefix}/lib/openmpi/mca_scd_pmf.la
%{_prefix}/lib/openmpi/mca_sec_munge.la
%{_prefix}/lib/openmpi/mca_sec_munge.so
%{_prefix}/lib/openmpi/mca_sensor_heartbeat.so
%{_prefix}/lib/openmpi/mca_sensor_heartbeat.la
%{_prefix}/lib/openmpi/mca_sensor_file.so
%{_prefix}/lib/openmpi/mca_sensor_file.la
%{_prefix}/lib/openmpi/mca_sensor_resusage.so
%{_prefix}/lib/openmpi/mca_sensor_resusage.la
%{_prefix}/lib/openmpi/mca_sensor_mcedata.so
%{_prefix}/lib/openmpi/mca_sensor_mcedata.la
%{_prefix}/lib/openmpi/mca_sensor_snmp.so
%{_prefix}/lib/openmpi/mca_sensor_snmp.la
%{_prefix}/lib/openmpi/mca_sensor_udsensors.so
%{_prefix}/lib/openmpi/mca_sensor_udsensors.la
%{_prefix}/lib/openmpi/mca_sensor_sigar.so
%{_prefix}/lib/openmpi/mca_sensor_sigar.la
%{_prefix}/lib/openmpi/mca_sensor_nodepower.so
%{_prefix}/lib/openmpi/mca_sensor_nodepower.la
%{_prefix}/lib/openmpi/mca_sensor_dmidata.so
%{_prefix}/lib/openmpi/mca_sensor_dmidata.la
%{_prefix}/lib/openmpi/mca_sensor_ipmi_ts.so
%{_prefix}/lib/openmpi/mca_sensor_ipmi_ts.la
%{_prefix}/lib/openmpi/mca_sensor_componentpower.so
%{_prefix}/lib/openmpi/mca_sensor_componentpower.la
%{_prefix}/lib/openmpi/mca_sensor_syslog.so
%{_prefix}/lib/openmpi/mca_sensor_syslog.la
%{_prefix}/lib/openmpi/mca_sensor_freq.so
%{_prefix}/lib/openmpi/mca_sensor_freq.la
%{_prefix}/lib/openmpi/mca_sensor_errcounts.so
%{_prefix}/lib/openmpi/mca_sensor_errcounts.la
%{_prefix}/lib/openmpi/mca_sensor_coretemp.so
%{_prefix}/lib/openmpi/mca_sensor_coretemp.la
%{_prefix}/lib/openmpi/mca_sensor_ipmi.so
%{_prefix}/lib/openmpi/mca_sensor_ipmi.la
%{_prefix}/lib/openmpi/mca_sst_tool.so
%{_prefix}/lib/openmpi/mca_sst_tool.la
%{_prefix}/lib/openmpi/mca_sst_orcmsched.so
%{_prefix}/lib/openmpi/mca_sst_orcmsched.la
%{_prefix}/lib/openmpi/mca_sst_orcmd.so
%{_prefix}/lib/openmpi/mca_sst_orcmd.la
%{_prefix}/lib/liborcmopen-rte.so.0.0.0
%{_prefix}/lib/liborcmopen-rte.so.0
%{_prefix}/lib/liborcmopen-rte.so
%{_prefix}/lib/liborcmopen-rte.la
%{_prefix}/lib/libsensysplugins.so.0.0.0
%{_prefix}/lib/libsensysplugins.so.0
%{_prefix}/lib/libsensysplugins.so
%{_prefix}/lib/libsensysplugins.la
%{_prefix}/lib/libsensysplugins_helper.so.0.0.0
%{_prefix}/lib/libsensysplugins_helper.so.0
%{_prefix}/lib/libsensysplugins_helper.so
%{_prefix}/lib/libsensysplugins_helper.la
%{_prefix}/lib/liborcm.so.0.0.0
%{_prefix}/lib/liborcm.so.0
%{_prefix}/lib/liborcm.so
%{_prefix}/lib/liborcm.la
%{_prefix}/share/openmpi/amca-param-sets/example.conf
%{_prefix}/share/openmpi/openmpi-valgrind.supp
%{_prefix}/share/openmpi/help-opal-util.txt
%{_prefix}/share/openmpi/help-mca-base.txt
%{_prefix}/share/openmpi/help-mca-var.txt
%{_prefix}/share/openmpi/help-dstore-base.txt
%{_prefix}/share/openmpi/help-opal-hwloc-base.txt
%{_prefix}/share/openmpi/help-pmix-base.txt
%{_prefix}/share/openmpi/help-opal-timer-linux.txt
%{_prefix}/share/openmpi/help-opal-runtime.txt
%{_prefix}/share/openmpi/help-opal_info.txt
%{_prefix}/share/openmpi/help-opal-wrapper.txt
%{_prefix}/share/openmpi/opalcc-wrapper-data.txt
%{_prefix}/share/openmpi/opalc++-wrapper-data.txt
%{_prefix}/share/openmpi/help-errmgr-base.txt
%{_prefix}/share/openmpi/help-ess-base.txt
%{_prefix}/share/openmpi/help-oob-base.txt
%{_prefix}/share/openmpi/help-plm-base.txt
%{_prefix}/share/openmpi/help-ras-base.txt
%{_prefix}/share/openmpi/help-orte-rmaps-base.txt
%{_prefix}/share/openmpi/help-orte-runtime.txt
%{_prefix}/share/openmpi/help-hostfile.txt
%{_prefix}/share/openmpi/help-dash-host.txt
%{_prefix}/share/openmpi/help-regex.txt
%{_prefix}/share/openmpi/help-ess-orcm.txt
%{_prefix}/share/openmpi/help-oob-tcp.txt
%{_prefix}/share/openmpi/help-ras-orcm.txt
%{_prefix}/share/openmpi/help-ras-simulator.txt
%{_prefix}/share/openmpi/help-orte-rmaps-ppr.txt
%{_prefix}/share/openmpi/help-routed-orcm.txt
%{_prefix}/share/openmpi/help-orcm-cfgi.txt
%{_prefix}/share/openmpi/help-orcm-sst.txt
%{_prefix}/share/openmpi/help-orcm-runtime.txt
%{_prefix}/share/openmpi/help-orcm-sensor-heartbeat.txt
%{_prefix}/share/openmpi/help-orcm-sensor-file.txt
%{_prefix}/share/openmpi/help-orcm-sensor-resusage.txt
%{_prefix}/share/openmpi/help-orcm-sensor-mcedata.txt
%{_prefix}/share/openmpi/help-orcm-sensor-snmp.txt
%{_prefix}/share/openmpi/help-orcm-sensor-sigar.txt
%{_prefix}/share/openmpi/help-orcm-sensor-dmidata.txt
%{_prefix}/share/openmpi/help-orcm-sensor-freq.txt
%{_prefix}/share/openmpi/help-orcm-sensor-errcounts.txt
%{_prefix}/share/openmpi/help-orcm-sensor-coretemp.txt
%{_prefix}/share/openmpi/help-orcm-sensor-ipmi.txt
%{_prefix}/share/openmpi/help-sst-tool.txt
%{_prefix}/share/openmpi/help-sst-orcmsched.txt
%{_prefix}/share/openmpi/help-sst-orcmd.txt
%{_prefix}/share/openmpi/help-octl.txt
%{_prefix}/share/openmpi/help-orcm-info.txt
%{_prefix}/share/openmpi/help-orcmd.txt
%{_prefix}/share/openmpi/help-orcmsched.txt
%{_prefix}/share/openmpi/help-orte-notifier-smtp.txt
%{_prefix}/share/openmpi/orcmcc-wrapper-data.txt
%{_prefix}/share/man/man7/opal_crs.7
%{_prefix}/share/man/man7/orte_hosts.7
%{_prefix}/share/man/man1/opal_wrapper.1
%{_prefix}/share/man/man1/opalcc.1
%{_prefix}/share/man/man1/opalc++.1
%{_prefix}/share/man/man1/octl.1
%{_prefix}/share/man/man1/orcm-info.1
%{_prefix}/share/man/man1/orcmd.1
%{_prefix}/share/man/man1/orcmsched.1
%{_prefix}/share/db-schema/sensys-schema.sql

%files devel
%dir %{_prefix}/lib/pkgconfig
%dir %{_prefix}/include
%dir %{_prefix}/include/openmpi
%dir %{_prefix}/include/openmpi/opal
%dir %{_prefix}/include/openmpi/opal/class
%dir %{_prefix}/include/openmpi/opal/datatype
%dir %{_prefix}/include/openmpi/opal/errhandler
%dir %{_prefix}/include/openmpi/opal/sys
%dir %{_prefix}/include/openmpi/opal/sys/amd64
%dir %{_prefix}/include/openmpi/opal/sys/powerpc
%dir %{_prefix}/include/openmpi/opal/sys/osx
%dir %{_prefix}/include/openmpi/opal/sys/ia32
%dir %{_prefix}/include/openmpi/opal/sys/sparcv9
%dir %{_prefix}/include/openmpi/opal/sys/mips
%dir %{_prefix}/include/openmpi/opal/sys/ia64
%dir %{_prefix}/include/openmpi/opal/sys/sync_builtin
%dir %{_prefix}/include/openmpi/opal/sys/arm
%dir %{_prefix}/include/openmpi/opal/util
%dir %{_prefix}/include/openmpi/opal/mca
%dir %{_prefix}/include/openmpi/opal/mca/base
%dir %{_prefix}/include/openmpi/opal/mca/backtrace
%dir %{_prefix}/include/openmpi/opal/mca/backtrace/base
%dir %{_prefix}/include/openmpi/opal/mca/crs
%dir %{_prefix}/include/openmpi/opal/mca/crs/base
%dir %{_prefix}/include/openmpi/opal/mca/dl
%dir %{_prefix}/include/openmpi/opal/mca/dl/base
%dir %{_prefix}/include/openmpi/opal/mca/dstore
%dir %{_prefix}/include/openmpi/opal/mca/dstore/base
%dir %{_prefix}/include/openmpi/opal/mca/event
%dir %{_prefix}/include/openmpi/opal/mca/event/base
%dir %{_prefix}/include/openmpi/opal/mca/event/libevent2022
%dir %{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent
%dir %{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/compat
%dir %{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/compat/sys
%dir %{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/WIN32-Code
%dir %{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/WIN32-Code/event2
%dir %{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include
%dir %{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2
%dir %{_prefix}/include/openmpi/opal/mca/hwloc
%dir %{_prefix}/include/openmpi/opal/mca/hwloc/base
%dir %{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110
%dir %{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc
%dir %{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include
%dir %{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc
%dir %{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/autogen
%dir %{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/private
%dir %{_prefix}/include/openmpi/opal/mca/if
%dir %{_prefix}/include/openmpi/opal/mca/if/base
%dir %{_prefix}/include/openmpi/opal/mca/installdirs
%dir %{_prefix}/include/openmpi/opal/mca/installdirs/base
%dir %{_prefix}/include/openmpi/opal/mca/pmix
%dir %{_prefix}/include/openmpi/opal/mca/pmix/base
%dir %{_prefix}/include/openmpi/opal/mca/pstat
%dir %{_prefix}/include/openmpi/opal/mca/pstat/base
%dir %{_prefix}/include/openmpi/opal/mca/sec
%dir %{_prefix}/include/openmpi/opal/mca/sec/base
%dir %{_prefix}/include/openmpi/opal/mca/timer
%dir %{_prefix}/include/openmpi/opal/mca/timer/base
%dir %{_prefix}/include/openmpi/opal/dss
%dir %{_prefix}/include/openmpi/opal/runtime
%dir %{_prefix}/include/openmpi/opal/memoryhooks
%dir %{_prefix}/include/openmpi/opal/threads
%dir %{_prefix}/include/openmpi/orte
%dir %{_prefix}/include/openmpi/orte/mca
%dir %{_prefix}/include/openmpi/orte/mca/errmgr
%dir %{_prefix}/include/openmpi/orte/mca/errmgr/base
%dir %{_prefix}/include/openmpi/orte/mca/ess
%dir %{_prefix}/include/openmpi/orte/mca/ess/base
%dir %{_prefix}/include/openmpi/orte/mca/notifier
%dir %{_prefix}/include/openmpi/orte/mca/notifier/base
%dir %{_prefix}/include/openmpi/orte/mca/oob
%dir %{_prefix}/include/openmpi/orte/mca/oob/base
%dir %{_prefix}/include/openmpi/orte/mca/plm
%dir %{_prefix}/include/openmpi/orte/mca/plm/base
%dir %{_prefix}/include/openmpi/orte/mca/ras
%dir %{_prefix}/include/openmpi/orte/mca/ras/base
%dir %{_prefix}/include/openmpi/orte/mca/rmaps
%dir %{_prefix}/include/openmpi/orte/mca/rmaps/base
%dir %{_prefix}/include/openmpi/orte/mca/rml
%dir %{_prefix}/include/openmpi/orte/mca/rml/base
%dir %{_prefix}/include/openmpi/orte/mca/routed
%dir %{_prefix}/include/openmpi/orte/mca/routed/base
%dir %{_prefix}/include/openmpi/orte/mca/state
%dir %{_prefix}/include/openmpi/orte/mca/state/base
%dir %{_prefix}/include/openmpi/orte/util
%dir %{_prefix}/include/openmpi/orte/util/dash_host
%dir %{_prefix}/include/openmpi/orte/util/hostfile
%dir %{_prefix}/include/openmpi/orte/runtime
%dir %{_prefix}/include/openmpi/orte/runtime/data_type_support
%dir %{_prefix}/include/openmpi/orcm
%dir %{_prefix}/include/openmpi/orcm/common
%dir %{_prefix}/include/openmpi/orcm/common/parsers
%dir %{_prefix}/include/openmpi/orcm/common/parsers/pugixml
%dir %{_prefix}/include/openmpi/orcm/mca
%dir %{_prefix}/include/openmpi/orcm/mca/analytics
%dir %{_prefix}/include/openmpi/orcm/mca/analytics/base
%dir %{_prefix}/include/openmpi/orcm/mca/cfgi
%dir %{_prefix}/include/openmpi/orcm/mca/cfgi/base
%dir %{_prefix}/include/openmpi/orcm/mca/db
%dir %{_prefix}/include/openmpi/orcm/mca/db/base
%dir %{_prefix}/include/openmpi/orcm/mca/diag
%dir %{_prefix}/include/openmpi/orcm/mca/diag/base
%dir %{_prefix}/include/openmpi/orcm/mca/dispatch
%dir %{_prefix}/include/openmpi/orcm/mca/dispatch/base
%dir %{_prefix}/include/openmpi/orcm/mca/parser
%dir %{_prefix}/include/openmpi/orcm/mca/parser/base
%dir %{_prefix}/include/openmpi/orcm/mca/scd
%dir %{_prefix}/include/openmpi/orcm/mca/scd/base
%dir %{_prefix}/include/openmpi/orcm/mca/sensor
%dir %{_prefix}/include/openmpi/orcm/mca/sensor/base
%dir %{_prefix}/include/openmpi/orcm/mca/sensor/ipmi
%dir %{_prefix}/include/openmpi/orcm/mca/sst
%dir %{_prefix}/include/openmpi/orcm/mca/sst/base
%dir %{_prefix}/include/openmpi/orcm/runtime
%dir %{_prefix}/include/openmpi/orcm/util
%dir %{_prefix}/include/openmpi/orcm/util/led_control
%{_prefix}/lib/pkgconfig/opal.pc
%{_prefix}/lib/pkgconfig/orcm.pc
%{_prefix}/include/openmpi/opal/sys/powerpc/atomic.h
%{_prefix}/include/openmpi/opal/sys/powerpc/timer.h
%{_prefix}/include/openmpi/opal/sys/osx/atomic.h
%{_prefix}/include/openmpi/opal/sys/ia32/atomic.h
%{_prefix}/include/openmpi/opal/sys/ia32/timer.h
%{_prefix}/include/openmpi/opal/sys/amd64/atomic.h
%{_prefix}/include/openmpi/opal/sys/amd64/timer.h
%{_prefix}/include/openmpi/opal/sys/sparcv9/atomic.h
%{_prefix}/include/openmpi/opal/sys/sparcv9/timer.h
%{_prefix}/include/openmpi/opal/sys/mips/atomic.h
%{_prefix}/include/openmpi/opal/sys/mips/timer.h
%{_prefix}/include/openmpi/opal/sys/ia64/atomic.h
%{_prefix}/include/openmpi/opal/sys/ia64/timer.h
%{_prefix}/include/openmpi/opal/sys/sync_builtin/atomic.h
%{_prefix}/include/openmpi/opal/sys/architecture.h
%{_prefix}/include/openmpi/opal/sys/atomic.h
%{_prefix}/include/openmpi/opal/sys/atomic_impl.h
%{_prefix}/include/openmpi/opal/sys/timer.h
%{_prefix}/include/openmpi/opal/sys/cma.h
%{_prefix}/include/openmpi/opal/sys/arm/atomic.h
%{_prefix}/include/openmpi/opal/sys/arm/timer.h
%{_prefix}/include/openmpi/opal/align.h
%{_prefix}/include/openmpi/opal/constants.h
%{_prefix}/include/openmpi/opal/opal_socket_errno.h
%{_prefix}/include/openmpi/opal/types.h
%{_prefix}/include/openmpi/opal/prefetch.h
%{_prefix}/include/openmpi/opal/hash_string.h
%{_prefix}/include/openmpi/opal/version.h
%{_prefix}/include/openmpi/opal/frameworks.h
%{_prefix}/include/openmpi/opal/opal_portable_platform.h
%{_prefix}/include/openmpi/opal/datatype/opal_convertor.h
%{_prefix}/include/openmpi/opal/datatype/opal_convertor_internal.h
%{_prefix}/include/openmpi/opal/datatype/opal_datatype_checksum.h
%{_prefix}/include/openmpi/opal/datatype/opal_datatype.h
%{_prefix}/include/openmpi/opal/datatype/opal_datatype_internal.h
%{_prefix}/include/openmpi/opal/datatype/opal_datatype_copy.h
%{_prefix}/include/openmpi/opal/datatype/opal_datatype_memcpy.h
%{_prefix}/include/openmpi/opal/datatype/opal_datatype_pack.h
%{_prefix}/include/openmpi/opal/datatype/opal_datatype_prototypes.h
%{_prefix}/include/openmpi/opal/datatype/opal_datatype_unpack.h
%{_prefix}/include/openmpi/opal/util/alfg.h
%{_prefix}/include/openmpi/opal/util/arch.h
%{_prefix}/include/openmpi/opal/util/argv.h
%{_prefix}/include/openmpi/opal/util/basename.h
%{_prefix}/include/openmpi/opal/util/bit_ops.h
%{_prefix}/include/openmpi/opal/util/cmd_line.h
%{_prefix}/include/openmpi/opal/util/crc.h
%{_prefix}/include/openmpi/opal/util/daemon_init.h
%{_prefix}/include/openmpi/opal/util/error.h
%{_prefix}/include/openmpi/opal/util/few.h
%{_prefix}/include/openmpi/opal/util/fd.h
%{_prefix}/include/openmpi/opal/util/fd.c
%{_prefix}/include/openmpi/opal/util/if.h
%{_prefix}/include/openmpi/opal/util/keyval_parse.h
%{_prefix}/include/openmpi/opal/util/malloc.h
%{_prefix}/include/openmpi/opal/util/net.h
%{_prefix}/include/openmpi/opal/util/numtostr.h
%{_prefix}/include/openmpi/opal/util/opal_environ.h
%{_prefix}/include/openmpi/opal/util/opal_getcwd.h
%{_prefix}/include/openmpi/opal/util/opal_pty.h
%{_prefix}/include/openmpi/opal/util/os_dirpath.h
%{_prefix}/include/openmpi/opal/util/os_path.h
%{_prefix}/include/openmpi/opal/util/output.h
%{_prefix}/include/openmpi/opal/util/path.h
%{_prefix}/include/openmpi/opal/util/printf.h
%{_prefix}/include/openmpi/opal/util/proc.h
%{_prefix}/include/openmpi/opal/util/qsort.h
%{_prefix}/include/openmpi/opal/util/show_help.h
%{_prefix}/include/openmpi/opal/util/show_help_lex.h
%{_prefix}/include/openmpi/opal/util/stacktrace.h
%{_prefix}/include/openmpi/opal/util/strncpy.h
%{_prefix}/include/openmpi/opal/util/sys_limits.h
%{_prefix}/include/openmpi/opal/util/timings.h
%{_prefix}/include/openmpi/opal/util/uri.h
%{_prefix}/include/openmpi/opal/mca/base/base.h
%{_prefix}/include/openmpi/opal/mca/base/mca_base_component_repository.h
%{_prefix}/include/openmpi/opal/mca/base/mca_base_var.h
%{_prefix}/include/openmpi/opal/mca/base/mca_base_pvar.h
%{_prefix}/include/openmpi/opal/mca/base/mca_base_var_enum.h
%{_prefix}/include/openmpi/opal/mca/base/mca_base_var_group.h
%{_prefix}/include/openmpi/opal/mca/base/mca_base_vari.h
%{_prefix}/include/openmpi/opal/mca/base/mca_base_framework.h
%{_prefix}/include/openmpi/opal/mca/backtrace/base/base.h
%{_prefix}/include/openmpi/opal/mca/backtrace/backtrace.h
%{_prefix}/include/openmpi/opal/mca/crs/base/base.h
%{_prefix}/include/openmpi/opal/mca/crs/crs.h
%{_prefix}/include/openmpi/opal/mca/dl/base/base.h
%{_prefix}/include/openmpi/opal/mca/dl/dl.h
%{_prefix}/include/openmpi/opal/mca/dstore/base/base.h
%{_prefix}/include/openmpi/opal/mca/dstore/dstore.h
%{_prefix}/include/openmpi/opal/mca/dstore/dstore_types.h
%{_prefix}/include/openmpi/opal/mca/event/base/base.h
%{_prefix}/include/openmpi/opal/mca/event/event.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/event-config.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/buffer_compat.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/buffer.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/bufferevent_compat.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/bufferevent_ssl.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/bufferevent_struct.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/bufferevent.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/dns_compat.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/dns_struct.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/event_compat.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/event_struct.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/event.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/http_compat.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/http_struct.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/http.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/keyvalq_struct.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/listener.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/rpc_compat.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/rpc_struct.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/rpc.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/tag_compat.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/tag.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/thread.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/include/event2/util.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/WIN32-Code/tree.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/WIN32-Code/event2/event-config.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/opal_rename.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/event.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/evutil.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/util-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/mm-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/ipv6-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/strlcpy-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/evbuffer-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/bufferevent-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/event-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/evthread-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/defer-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/minheap-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/log-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/evsignal-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/evmap-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/changelist-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/iocp-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/ratelim-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/evhttp.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/http-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/ht-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/evrpc.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/evrpc-internal.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/evdns.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent/compat/sys/queue.h
%{_prefix}/include/openmpi/opal/mca/event/libevent2022/libevent2022.h
%{_prefix}/include/openmpi/opal/mca/hwloc/base/base.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/autogen/config.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/bitmap.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/cuda.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/cudart.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/deprecated.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/diff.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/gl.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/helper.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/inlines.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/intel-mic.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/myriexpress.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/nvml.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/opencl.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/openfabrics-verbs.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/plugins.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/rename.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/linux.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/linux-libnuma.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc/glibc-sched.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/private/private.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/private/debug.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/private/misc.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/private/cpuid-x86.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc/include/hwloc.h
%{_prefix}/include/openmpi/opal/mca/hwloc/hwloc1110/hwloc1110.h
%{_prefix}/include/openmpi/opal/mca/if/base/base.h
%{_prefix}/include/openmpi/opal/mca/if/if.h
%{_prefix}/include/openmpi/opal/mca/installdirs/base/base.h
%{_prefix}/include/openmpi/opal/mca/installdirs/installdirs.h
%{_prefix}/include/openmpi/opal/mca/pmix/base/base.h
%{_prefix}/include/openmpi/opal/mca/pmix/base/pmix_base_fns.h
%{_prefix}/include/openmpi/opal/mca/pmix/pmix.h
%{_prefix}/include/openmpi/opal/mca/pstat/base/base.h
%{_prefix}/include/openmpi/opal/mca/pstat/pstat.h
%{_prefix}/include/openmpi/opal/mca/sec/base/base.h
%{_prefix}/include/openmpi/opal/mca/sec/sec.h
%{_prefix}/include/openmpi/opal/mca/timer/base/base.h
%{_prefix}/include/openmpi/opal/mca/timer/base/timer_base_null.h
%{_prefix}/include/openmpi/opal/mca/timer/timer.h
%{_prefix}/include/openmpi/opal/mca/mca.h
%{_prefix}/include/openmpi/opal/dss/dss.h
%{_prefix}/include/openmpi/opal/dss/dss_types.h
%{_prefix}/include/openmpi/opal/dss/dss_internal.h
%{_prefix}/include/openmpi/opal/errhandler/opal_errhandler.h
%{_prefix}/include/openmpi/opal/runtime/opal_progress.h
%{_prefix}/include/openmpi/opal/runtime/opal.h
%{_prefix}/include/openmpi/opal/runtime/opal_cr.h
%{_prefix}/include/openmpi/opal/runtime/opal_info_support.h
%{_prefix}/include/openmpi/opal/runtime/opal_params.h
%{_prefix}/include/openmpi/opal/runtime/opal_progress_threads.h
%{_prefix}/include/openmpi/opal/memoryhooks/memory.h
%{_prefix}/include/openmpi/opal/memoryhooks/memory_internal.h
%{_prefix}/include/openmpi/opal/class/opal_bitmap.h
%{_prefix}/include/openmpi/opal/class/opal_hash_table.h
%{_prefix}/include/openmpi/opal/class/opal_hotel.h
%{_prefix}/include/openmpi/opal/class/opal_tree.h
%{_prefix}/include/openmpi/opal/class/opal_list.h
%{_prefix}/include/openmpi/opal/class/opal_object.h
%{_prefix}/include/openmpi/opal/class/opal_graph.h
%{_prefix}/include/openmpi/opal/class/opal_lifo.h
%{_prefix}/include/openmpi/opal/class/opal_fifo.h
%{_prefix}/include/openmpi/opal/class/opal_pointer_array.h
%{_prefix}/include/openmpi/opal/class/opal_value_array.h
%{_prefix}/include/openmpi/opal/class/opal_ring_buffer.h
%{_prefix}/include/openmpi/opal/threads/condition.h
%{_prefix}/include/openmpi/opal/threads/mutex.h
%{_prefix}/include/openmpi/opal/threads/mutex_unix.h
%{_prefix}/include/openmpi/opal/threads/threads.h
%{_prefix}/include/openmpi/opal/threads/tsd.h
%{_prefix}/include/openmpi/opal_config.h
%{_prefix}/include/openmpi/opal_config_top.h
%{_prefix}/include/openmpi/opal_config_bottom.h
%{_prefix}/include/openmpi/opal_stdint.h
%{_prefix}/include/openmpi/orte_config.h
%{_prefix}/include/openmpi/orte/version.h
%{_prefix}/include/openmpi/orte/constants.h
%{_prefix}/include/openmpi/orte/types.h
%{_prefix}/include/openmpi/orte/frameworks.h
%{_prefix}/include/openmpi/orte/mca/errmgr/base/errmgr_private.h
%{_prefix}/include/openmpi/orte/mca/errmgr/base/base.h
%{_prefix}/include/openmpi/orte/mca/errmgr/errmgr.h
%{_prefix}/include/openmpi/orte/mca/ess/base/base.h
%{_prefix}/include/openmpi/orte/mca/ess/ess.h
%{_prefix}/include/openmpi/orte/mca/notifier/base/base.h
%{_prefix}/include/openmpi/orte/mca/notifier/notifier.h
%{_prefix}/include/openmpi/orte/mca/oob/base/base.h
%{_prefix}/include/openmpi/orte/mca/oob/oob.h
%{_prefix}/include/openmpi/orte/mca/plm/base/base.h
%{_prefix}/include/openmpi/orte/mca/plm/base/plm_private.h
%{_prefix}/include/openmpi/orte/mca/plm/plm.h
%{_prefix}/include/openmpi/orte/mca/plm/plm_types.h
%{_prefix}/include/openmpi/orte/mca/ras/base/base.h
%{_prefix}/include/openmpi/orte/mca/ras/base/ras_private.h
%{_prefix}/include/openmpi/orte/mca/ras/ras.h
%{_prefix}/include/openmpi/orte/mca/ras/ras_types.h
%{_prefix}/include/openmpi/orte/mca/rmaps/base/base.h
%{_prefix}/include/openmpi/orte/mca/rmaps/base/rmaps_private.h
%{_prefix}/include/openmpi/orte/mca/rmaps/rmaps.h
%{_prefix}/include/openmpi/orte/mca/rmaps/rmaps_types.h
%{_prefix}/include/openmpi/orte/mca/rml/base/base.h
%{_prefix}/include/openmpi/orte/mca/rml/base/rml_contact.h
%{_prefix}/include/openmpi/orte/mca/rml/rml.h
%{_prefix}/include/openmpi/orte/mca/rml/rml_types.h
%{_prefix}/include/openmpi/orte/mca/routed/base/base.h
%{_prefix}/include/openmpi/orte/mca/routed/routed.h
%{_prefix}/include/openmpi/orte/mca/routed/routed_types.h
%{_prefix}/include/openmpi/orte/mca/state/base/state_private.h
%{_prefix}/include/openmpi/orte/mca/state/base/base.h
%{_prefix}/include/openmpi/orte/mca/state/state.h
%{_prefix}/include/openmpi/orte/mca/state/state_types.h
%{_prefix}/include/openmpi/orte/mca/mca.h
%{_prefix}/include/openmpi/orte/util/dash_host/dash_host.h
%{_prefix}/include/openmpi/orte/util/name_fns.h
%{_prefix}/include/openmpi/orte/util/proc_info.h
%{_prefix}/include/openmpi/orte/util/session_dir.h
%{_prefix}/include/openmpi/orte/util/show_help.h
%{_prefix}/include/openmpi/orte/util/error_strings.h
%{_prefix}/include/openmpi/orte/util/context_fns.h
%{_prefix}/include/openmpi/orte/util/parse_options.h
%{_prefix}/include/openmpi/orte/util/pre_condition_transports.h
%{_prefix}/include/openmpi/orte/util/hnp_contact.h
%{_prefix}/include/openmpi/orte/util/nidmap.h
%{_prefix}/include/openmpi/orte/util/regex.h
%{_prefix}/include/openmpi/orte/util/attr.h
%{_prefix}/include/openmpi/orte/util/listener.h
%{_prefix}/include/openmpi/orte/util/hostfile/hostfile.h
%{_prefix}/include/openmpi/orte/util/hostfile/hostfile_lex.h
%{_prefix}/include/openmpi/orte/runtime/runtime.h
%{_prefix}/include/openmpi/orte/runtime/orte_locks.h
%{_prefix}/include/openmpi/orte/runtime/orte_globals.h
%{_prefix}/include/openmpi/orte/runtime/orte_quit.h
%{_prefix}/include/openmpi/orte/runtime/runtime_internals.h
%{_prefix}/include/openmpi/orte/runtime/orte_wait.h
%{_prefix}/include/openmpi/orte/runtime/orte_cr.h
%{_prefix}/include/openmpi/orte/runtime/orte_data_server.h
%{_prefix}/include/openmpi/orte/runtime/orte_info_support.h
%{_prefix}/include/openmpi/orte/runtime/data_type_support/orte_dt_support.h
%{_prefix}/include/openmpi/orcm/frameworks.h
%{_prefix}/include/openmpi/orcm/version.h
%{_prefix}/include/openmpi/orcm/constants.h
%{_prefix}/include/openmpi/orcm/types.h
%{_prefix}/include/openmpi/orcm/common/dataContainer.hpp
%{_prefix}/include/openmpi/orcm/common/UDExceptions.h
%{_prefix}/include/openmpi/orcm/common/udsensors.h
%{_prefix}/include/openmpi/orcm/common/baseFactory.h
%{_prefix}/include/openmpi/orcm/common/dataContainerHelper.hpp
%{_prefix}/include/openmpi/orcm/common/sensorconfig.hpp
%{_prefix}/include/openmpi/orcm/common/sensorConfigNodeXml.hpp
%{_prefix}/include/openmpi/orcm/common/xmlManager.hpp
%{_prefix}/include/openmpi/orcm/common/parsers/pugixml/pugixml.cpp
%{_prefix}/include/openmpi/orcm/common/parsers/pugixml/pugixml.hpp
%{_prefix}/include/openmpi/orcm/common/parsers/pugixml/pugiconfig.hpp
%{_prefix}/include/openmpi/orcm/mca/analytics/base/analytics_private.h
%{_prefix}/include/openmpi/orcm/mca/analytics/base/base.h
%{_prefix}/include/openmpi/orcm/mca/analytics/base/analytics_factory.h
%{_prefix}/include/openmpi/orcm/mca/analytics/base/c_analytics_factory.h
%{_prefix}/include/openmpi/orcm/mca/analytics/analytics.h
%{_prefix}/include/openmpi/orcm/mca/analytics/analytics_types.h
%{_prefix}/include/openmpi/orcm/mca/analytics/analytics_interface.h
%{_prefix}/include/openmpi/orcm/mca/cfgi/base/base.h
%{_prefix}/include/openmpi/orcm/mca/cfgi/cfgi.h
%{_prefix}/include/openmpi/orcm/mca/cfgi/cfgi_types.h
%{_prefix}/include/openmpi/orcm/mca/db/base/base.h
%{_prefix}/include/openmpi/orcm/mca/db/db.h
%{_prefix}/include/openmpi/orcm/mca/diag/base/base.h
%{_prefix}/include/openmpi/orcm/mca/diag/diag.h
%{_prefix}/include/openmpi/orcm/mca/dispatch/base/base.h
%{_prefix}/include/openmpi/orcm/mca/dispatch/dispatch.h
%{_prefix}/include/openmpi/orcm/mca/dispatch/dispatch_types.h
%{_prefix}/include/openmpi/orcm/mca/parser/base/base.h
%{_prefix}/include/openmpi/orcm/mca/parser/parser.h
%{_prefix}/include/openmpi/orcm/mca/scd/base/base.h
%{_prefix}/include/openmpi/orcm/mca/scd/scd.h
%{_prefix}/include/openmpi/orcm/mca/scd/scd_types.h
%{_prefix}/include/openmpi/orcm/mca/sensor/base/base.h
%{_prefix}/include/openmpi/orcm/mca/sensor/base/sensor_private.h
%{_prefix}/include/openmpi/orcm/mca/sensor/base/sensor_measurement.h
%{_prefix}/include/openmpi/orcm/mca/sensor/base/sensor_runtime_metrics.h
%{_prefix}/include/openmpi/orcm/mca/sensor/sensor.h
%{_prefix}/include/openmpi/orcm/mca/sensor/sensor_types.h
%{_prefix}/include/openmpi/orcm/mca/sensor/ipmi/ipmi_collector.h
%{_prefix}/include/openmpi/orcm/mca/sensor/ipmi/ipmi_parser.h
%{_prefix}/include/openmpi/orcm/mca/sensor/ipmi/ipmi_parser_interface.h
%{_prefix}/include/openmpi/orcm/mca/sst/base/base.h
%{_prefix}/include/openmpi/orcm/mca/sst/sst.h
%{_prefix}/include/openmpi/orcm/mca/mca.h
%{_prefix}/include/openmpi/orcm/runtime/runtime.h
%{_prefix}/include/openmpi/orcm/runtime/orcm_globals.h
%{_prefix}/include/openmpi/orcm/runtime/orcm_info_support.h
%{_prefix}/include/openmpi/orcm/runtime/orcm_cmd_server.h
%{_prefix}/include/openmpi/orcm/util/utils.h
%{_prefix}/include/openmpi/orcm/util/cli.h
%{_prefix}/include/openmpi/orcm/util/attr.h
%{_prefix}/include/openmpi/orcm/util/dlopen_helper.h
%{_prefix}/include/openmpi/orcm/util/logical_group.h
%{_prefix}/include/openmpi/orcm/util/vardata.h
%{_prefix}/include/openmpi/orcm/util/string_utils.h
%{_prefix}/include/openmpi/orcm/util/led_control/ipmicmd_wrapper.h
%{_prefix}/include/openmpi/orcm/util/led_control/led_control.h
%{_prefix}/include/openmpi/orcm/util/led_control/led_control_interface.h
%{_prefix}/include/openmpi/orcm_config.h

%files common
%if 0%{?suse_version} > 1200
/var/adm/fillup-templates/sysconfig.orcmd
/var/adm/fillup-templates/sysconfig.orcmsched
%else
/etc/sysconfig/orcmd
/etc/sysconfig/orcmsched
%endif

%post -p /sbin/ldconfig
%fillup_only

%postun -p /sbin/ldconfig
%endif
# End of filelist for dynamic compilation

%changelog
* Fri May 22 2017 Erich Cordoba <erich.cordoba.malibran@intel.com> - 0.32.0-1.1
- Enable static and dynamic building in the same spec file

* Tue May 16 2017 Erich Cordoba <erich.cordoba.malibran@intel.com> - 0.32.0-1.1
- Enable global definitions

* Fri May 12 2017 Erich Cordoba <erich.cordoba.malibran@intel.com> - 0.32.0-2
- Update dependencies for CentOS 7.3 and SLES 12 SP2

* Mon Feb 13 2017 David Romero <david.romero.antequera@intel.com> - 0.32.0-2
- Modifying spec file in order to support both CentOS and SuSE

* Fri Feb 03 2017 Erich Cordoba <erich.cordoba.malibran@intel.com> - 0.32.0-2
- Adding first version of Sensys spec file

endef

export make_spec
