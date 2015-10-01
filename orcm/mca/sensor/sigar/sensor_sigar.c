/*
 * Copyright (c) 2013-2015 Intel, Inc. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "orcm_config.h"
#include "orcm/constants.h"
#include "orcm/types.h"

#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif  /* HAVE_UNISTD_H */
#ifdef HAVE_STRING_H
#include <string.h>
#endif  /* HAVE_STRING_H */
#include <stdio.h>
#include <math.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif

#ifdef ORCM_SIGAR_LINUX
#include <sigar.h>
#else
#include <libsigar-universal-macosx>
#endif

#include "opal_stdint.h"
#include "opal/class/opal_pointer_array.h"
#include "opal/class/opal_ring_buffer.h"
#include "opal/dss/dss.h"
#include "opal/util/output.h"
#include "opal/mca/pstat/pstat.h"
#include "opal/mca/event/event.h"
#include "opal/runtime/opal_progress_threads.h"

#include "orte/mca/errmgr/errmgr.h"

#include "orcm/mca/db/db.h"
#include "orcm/runtime/orcm_globals.h"

#include "orcm/mca/sensor/base/base.h"
#include "orcm/mca/sensor/base/sensor_private.h"
#include "sensor_sigar.h"

/* declare the API functions */
static int init(void);
static void finalize(void);
static void start(orte_jobid_t job);
static void stop(orte_jobid_t job);
static void sigar_sample(orcm_sensor_sampler_t *sampler);
static void perthread_sigar_sample(int fd, short args, void *cbdata);
static void collect_sample(orcm_sensor_sampler_t *sampler);
static void sigar_log(opal_buffer_t *buf);
static void sigar_set_sample_rate(int sample_rate);
static void sigar_get_sample_rate(int *sample_rate);
static void sigar_inventory_collect(opal_buffer_t *inventory_snapshot);
static void sigar_inventory_log(char *hostname, opal_buffer_t *inventory_snapshot);

/* instantiate the module */
orcm_sensor_base_module_t orcm_sensor_sigar_module = {
    init,
    finalize,
    start,
    stop,
    sigar_sample,
    sigar_log,
    sigar_inventory_collect,
    sigar_inventory_log,
    sigar_set_sample_rate,
    sigar_get_sample_rate
};

/* define some local classes */
typedef struct {
    opal_list_item_t super;
    char *interface;
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t tx_packets;
    uint64_t tx_bytes;
} sensor_sigar_interface_t;
static void sit_cons(sensor_sigar_interface_t *sit)
{
    sit->interface = NULL;
    sit->rx_packets = 0;
    sit->rx_bytes = 0;
    sit->tx_packets = 0;
    sit->tx_bytes = 0;
}
static void sit_dest(sensor_sigar_interface_t *sit)
{
    if (NULL != sit->interface) {
        free(sit->interface);
    }
}
OBJ_CLASS_INSTANCE(sensor_sigar_interface_t,
                   opal_list_item_t,
                   sit_cons, sit_dest);

typedef struct {
    opal_list_item_t super;
    char *mount_pt;
    uint64_t reads;
    uint64_t writes;
    uint64_t read_bytes;
    uint64_t write_bytes;
} sensor_sigar_disks_t;
static void dit_cons(sensor_sigar_disks_t *dit)
{
    dit->mount_pt = NULL;
    dit->reads = 0;
    dit->writes = 0;
    dit->read_bytes = 0;
    dit->write_bytes = 0;
}
static void dit_dest(sensor_sigar_disks_t *dit)
{
    if (NULL != dit->mount_pt) {
        free(dit->mount_pt);
    }
}
OBJ_CLASS_INSTANCE(sensor_sigar_disks_t,
                   opal_list_item_t,
                   dit_cons, dit_dest);

static sigar_t *sigar;
static opal_list_t fslist;
static opal_list_t netlist;
static time_t last_sample = 0;
static struct cpu_data_t {
    uint64_t user;
    uint64_t nice;
    uint64_t sys;
    uint64_t idle;
    uint64_t wait;
    uint64_t total;
} pcpu;
static struct swap_data_t {
    uint64_t page_in;
    uint64_t page_out;
} pswap;
static bool log_enabled = true;
static opal_buffer_t test_vector;
static orcm_sensor_sampler_t *sigar_sampler = NULL;
static orcm_sensor_sigar_t orcm_sensor_sigar;

static uint64_t metric_diff_calc(sigar_uint64_t newval, uint64_t oldval,
                                 const char *name_for_log,
                                 const char* value_name_for_log);
static void generate_test_vector(opal_buffer_t *v);

static int init(void)
{
    sigar_file_system_list_t sigar_fslist;
    sigar_net_interface_list_t sigar_netlist;
    sensor_sigar_disks_t *dit;
    sensor_sigar_interface_t *sit;
    unsigned int i;

    if (mca_sensor_sigar_component.test) {
        /* generate test vector */
        OBJ_CONSTRUCT(&test_vector, opal_buffer_t);
        generate_test_vector(&test_vector);
        return ORCM_SUCCESS;
    }

    /* setup the globals */
    OBJ_CONSTRUCT(&fslist, opal_list_t);
    OBJ_CONSTRUCT(&netlist, opal_list_t);
    pcpu.user = 0;
    pcpu.nice = 0;
    pcpu.sys = 0;
    pcpu.idle = 0;
    pcpu.wait = 0;
    pcpu.total = 0;
    pswap.page_in = 0;
    pswap.page_out = 0;

    /* initialize sigar */
    if (0 != sigar_open(&sigar)) {
        opal_output(0, "%s: sigar_open failed on node %s",
                    ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                    orte_process_info.nodename);
        return ORTE_ERROR;
    }

    /* load the disk list */
    if (0 != sigar_file_system_list_get(sigar, &sigar_fslist)) {
        return ORTE_ERROR;
    }
    for (i = 0; i < sigar_fslist.number; i++) {
        if (sigar_fslist.data[i].type == SIGAR_FSTYPE_LOCAL_DISK || sigar_fslist.data[i].type == SIGAR_FSTYPE_NETWORK) {
            dit = OBJ_NEW(sensor_sigar_disks_t);
            dit->mount_pt = strdup(sigar_fslist.data[i].dir_name);
            opal_list_append(&fslist, &dit->super);
        }
    }
    sigar_file_system_list_destroy(sigar, &sigar_fslist);

    /* load the list of network interfaces */
    if (0 != sigar_net_interface_list_get(sigar, &sigar_netlist)) {
        return ORTE_ERROR;
    }
    for (i=0; i < sigar_netlist.number; i++) {
        sit = OBJ_NEW(sensor_sigar_interface_t);
        sit->interface = strdup(sigar_netlist.data[i]);
        opal_list_append(&netlist, &sit->super);
    }
    sigar_net_interface_list_destroy(sigar, &sigar_netlist);

    return ORCM_SUCCESS;
}

static void finalize(void)
{
    opal_list_item_t *item;

    if (mca_sensor_sigar_component.test) {
        /* destruct test vector */
        OBJ_DESTRUCT(&test_vector);
        return;
    }

    if (NULL != sigar) {
        sigar_close(sigar);
    }
    while (NULL != (item = opal_list_remove_first(&fslist))) {
        OBJ_RELEASE(item);
    }
    OBJ_DESTRUCT(&fslist);
    while (NULL != (item = opal_list_remove_first(&netlist))) {
        OBJ_RELEASE(item);
    }
    OBJ_DESTRUCT(&netlist);

    return;
}

/*
 * Start monitoring of local processes
 */
static void start(orte_jobid_t jobid)
{
    /* start a separate sigar progress thread for sampling */
    if (mca_sensor_sigar_component.use_progress_thread) {
        if (!orcm_sensor_sigar.ev_active) {
            orcm_sensor_sigar.ev_active = true;
            if (NULL == (orcm_sensor_sigar.ev_base = opal_progress_thread_init("sigar"))) {
                orcm_sensor_sigar.ev_active = false;
                return;
            }
        }

        /* setup sigar sampler */
        sigar_sampler = OBJ_NEW(orcm_sensor_sampler_t);

        /* check if sigar sample rate is provided for this*/
        if (!mca_sensor_sigar_component.sample_rate) {
            mca_sensor_sigar_component.sample_rate = orcm_sensor_base.sample_rate;
        }
        sigar_sampler->rate.tv_sec = mca_sensor_sigar_component.sample_rate;
        sigar_sampler->log_data = orcm_sensor_base.log_samples;
        opal_event_evtimer_set(orcm_sensor_sigar.ev_base, &sigar_sampler->ev,
                               perthread_sigar_sample, sigar_sampler);
        opal_event_evtimer_add(&sigar_sampler->ev, &sigar_sampler->rate);
    }else{
	 mca_sensor_sigar_component.sample_rate = orcm_sensor_base.sample_rate;

    }

    return;
}

static void stop(orte_jobid_t jobid)
{
    if (orcm_sensor_sigar.ev_active) {
        orcm_sensor_sigar.ev_active = false;
        /* stop the thread without releasing the event base */
        opal_progress_thread_pause("sigar");
    }
    return;
}

static int sigar_collect_mem(opal_buffer_t *dataptr)
{
    int rc;
    sigar_mem_t mem;
    char *error_string;
    bool log_group = true;
    /* get the memory usage for this node */
    memset(&mem, 0, sizeof(mem));
    if (SIGAR_OK != (rc = sigar_mem_get(sigar, &mem))) {
        error_string = strerror(rc);
        opal_output(0, "sigar_mem_get failed: %s", error_string);
        log_group = false;
    }
    opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                        "mem total: %" PRIu64 " used: %" PRIu64 " actual used: %" PRIu64 " actual free: %" PRIu64 "",
                        (uint64_t)mem.total, (uint64_t)mem.used, (uint64_t)mem.actual_used, (uint64_t)mem.actual_free);

    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &log_group, 1, OPAL_BOOL))) {
        return rc;
    }
    if (false == log_group)
        return ORCM_ERR_SENSOR_READ_FAIL;

    /* add it to the dataptr */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &mem.total, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &mem.used, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &mem.actual_used, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &mem.actual_free, 1, OPAL_UINT64))) {
        return rc;
    }
    return ORCM_SUCCESS;
}

static int sigar_collect_swap(opal_buffer_t *dataptr)
{
    int rc;
    sigar_swap_t swap;
    char *error_string;
    uint64_t ui64;
    bool log_group = true;
    /* get swap data */
    memset(&swap, 0, sizeof(swap));
    if (SIGAR_OK != (rc = sigar_swap_get(sigar, &swap))) {
        error_string = strerror(rc);
        opal_output(0, "sigar_swap_get failed: %s", error_string);
        log_group = false;
    }
    opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                        "swap total: %" PRIu64 " used: %" PRIu64 "page_in: %" PRIu64 " page_out: %" PRIu64 "\n",
                        (uint64_t)swap.total, (uint64_t)swap.used, (uint64_t)swap.page_in, (uint64_t)swap.page_out);

    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &log_group, 1, OPAL_BOOL))) {
        return rc;
    }
    if (false == log_group)
        return ORCM_ERR_SENSOR_READ_FAIL;
    /* compute the values we actually want and add them to the dataptr */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &swap.total, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &swap.used, 1, OPAL_UINT64))) {
        return rc;
    }
    ui64 = swap.page_in - pswap.page_in;
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &ui64, 1, OPAL_UINT64))) {
        return rc;
    }
    ui64 = swap.page_out - pswap.page_out;
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &ui64, 1, OPAL_UINT64))) {
        return rc;
    }
    return ORCM_SUCCESS;

}

static int sigar_collect_cpu(opal_buffer_t *dataptr)
{
    int rc;
    sigar_cpu_t cpu;
    char *error_string;
    double cpu_diff;
    float tmp;
    bool log_group = true;
    /* get the cpu usage */
    memset(&cpu, 0, sizeof(cpu));
    if (SIGAR_OK != (rc = sigar_cpu_get(sigar, &cpu))) {
        error_string = strerror(rc);
        opal_output(0, "sigar_cpu_get failed: %s", error_string);
        log_group = false;

    }
    opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                        "cpu user: %" PRIu64 " sys: %" PRIu64 " idle: %" PRIu64 " wait: %" PRIu64 " nice: %" PRIu64 " total: %" PRIu64 "",
            (uint64_t)cpu.user, (uint64_t)cpu.sys, (uint64_t)cpu.idle, (uint64_t)cpu.wait, (uint64_t)cpu.nice, (uint64_t)cpu.total);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &log_group, 1, OPAL_BOOL))) {
        return rc;
    }
    if (false == log_group)
        return ORCM_ERR_SENSOR_READ_FAIL;

    /* compute the values we actually want and add them to the dataptr */
    cpu_diff = (double)(cpu.total - pcpu.total);
    tmp = (float)((cpu.user - pcpu.user) * 100.0 / cpu_diff) + (float)((cpu.nice - pcpu.nice) * 100.0 / cpu_diff);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &tmp, 1, OPAL_FLOAT))) {
        return rc;
    }
    tmp = ((float) (cpu.sys - pcpu.sys) * 100.0 / cpu_diff) + ((float)((cpu.wait - pcpu.wait) * 100.0 / cpu_diff));
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &tmp, 1, OPAL_FLOAT))) {
        return rc;
    }
    tmp = (float) (cpu.idle - pcpu.idle) * 100.0 / cpu_diff;
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &tmp, 1, OPAL_FLOAT))) {
        return rc;
    }
    /* update the values */
    pcpu.user = cpu.user;
    pcpu.nice = cpu.nice;
    pcpu.sys = cpu.sys;
    pcpu.wait = cpu.wait;
    pcpu.idle = cpu.idle;
    pcpu.total = cpu.total;

    return ORCM_SUCCESS;
}

static int sigar_collect_load(opal_buffer_t *dataptr)
{
    int rc;
    sigar_loadavg_t loadavg;
    char *error_string;
    float tmp;
    bool log_group = true;

    /* get load average data */
    memset(&loadavg, 0, sizeof(loadavg));
    if (SIGAR_OK != (rc = sigar_loadavg_get(sigar, &loadavg))) {
        error_string = strerror(rc);
        opal_output(0, "sigar_loadavg_get failed: %s", error_string);
        log_group = false;
    }
    opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                        "load_avg: %e %e %e",
                        loadavg.loadavg[0], loadavg.loadavg[1], loadavg.loadavg[2]);

    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &log_group, 1, OPAL_BOOL))) {
        return rc;
    }
    if (false == log_group)
        return ORCM_ERR_SENSOR_READ_FAIL;
    /* add them to the dataptr */
    tmp = (float)loadavg.loadavg[0];
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &tmp, 1, OPAL_FLOAT))) {
        return rc;
    }
    tmp = (float)loadavg.loadavg[1];
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &tmp, 1, OPAL_FLOAT))) {
        return rc;
    }
    tmp = (float)loadavg.loadavg[2];
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &tmp, 1, OPAL_FLOAT))) {
        return rc;
    }
    return ORCM_SUCCESS;
}

static int sigar_collect_disk(opal_buffer_t *dataptr, double tdiff)
{
    int rc;
    sigar_disk_usage_t tdisk;
    sensor_sigar_disks_t *dit;
    sigar_file_system_usage_t fsusage;
    uint64_t reads, writes, read_bytes, write_bytes, read_time, write_time, io_time;
    uint64_t total_reads = 0, total_writes = 0, total_read_bytes = 0, total_write_bytes = 0;
    char *error_string;
    bool log_group = true;

    /* get disk usage data */
    memset(&tdisk, 0, sizeof(tdisk));
    OPAL_LIST_FOREACH(dit, &fslist, sensor_sigar_disks_t) {
        if (SIGAR_OK != (rc = sigar_file_system_usage_get(sigar, dit->mount_pt, &fsusage))) {
            error_string = strerror(rc);
            opal_output(0, "sigar_file_system_usage_get failed: %s", error_string);
            opal_output(0, "%s Failed to get usage data for filesystem %s",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), dit->mount_pt);
            log_group = false;
        } else {
            opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                                "FileSystem: %s Reads: %" PRIu64 " Writes: %" PRIu64 " ReadBytes: %" PRIu64 " WriteBytes: %" PRIu64 "",
                                dit->mount_pt, (uint64_t)fsusage.disk.reads, (uint64_t)fsusage.disk.writes, (uint64_t)fsusage.disk.read_bytes, (uint64_t)fsusage.disk.write_bytes);
            /* compute the number of reads since last reading */
            reads = metric_diff_calc(fsusage.disk.reads, dit->reads, dit->mount_pt, "disk reads");
            dit->reads = fsusage.disk.reads;  /* old = new */
            /* compute the number of writes since last reading */
            writes = metric_diff_calc(fsusage.disk.writes, dit->writes, dit->mount_pt, "disk writes");
            dit->writes = fsusage.disk.writes;  /* old = new */
            /* compute the number of read bytes since last reading */
            read_bytes = metric_diff_calc(fsusage.disk.read_bytes, dit->read_bytes, dit->mount_pt, "disk read bytes");
            dit->read_bytes = fsusage.disk.read_bytes;  /* old = new */
            /* compute the number of bytes written since last reading */
            write_bytes = metric_diff_calc(fsusage.disk.write_bytes, dit->write_bytes, dit->mount_pt, "disk write bytes");
            dit->write_bytes = fsusage.disk.write_bytes;  /* old = new */
            opal_output_verbose(4, orcm_sensor_base_framework.framework_output,
                                "FileSystem: %s ReadsChange: %" PRIu64 " WritesChange: %" PRIu64 " ReadBytesChange: %" PRIu64 " WriteBytesChange: %" PRIu64 "",
                                dit->mount_pt, (uint64_t)reads, (uint64_t)writes, (uint64_t)read_bytes, (uint64_t)write_bytes);
            /* accumulate the values */
            tdisk.reads += reads;
            tdisk.writes += writes;
            tdisk.read_bytes += read_bytes;
            tdisk.write_bytes += write_bytes;
            tdisk.rtime += fsusage.disk.rtime;
            tdisk.wtime += fsusage.disk.wtime;
            tdisk.qtime += fsusage.disk.qtime;
            total_reads += fsusage.disk.reads;
            total_writes += fsusage.disk.writes;
            total_read_bytes += fsusage.disk.read_bytes;
            total_write_bytes += fsusage.disk.write_bytes;
        }
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &log_group, 1, OPAL_BOOL))) {
        return rc;
    }
    if (false == log_group)
        return ORCM_ERR_SENSOR_READ_FAIL;
    opal_output_verbose(4, orcm_sensor_base_framework.framework_output,
                        "Totals: ReadsChange: %" PRIu64 " WritesChange: %" PRIu64 " ReadBytesChange: %" PRIu64 " WriteBytesChange: %" PRIu64 "",
                        (uint64_t)tdisk.reads, (uint64_t)tdisk.writes, (uint64_t)tdisk.read_bytes, (uint64_t)tdisk.write_bytes);
    opal_output_verbose(4, orcm_sensor_base_framework.framework_output,
                        "Totals: ReadTime: %" PRIu64 " WriteTime: %" PRIu64 " ioTime: %" PRIu64 "",
                        (uint64_t)tdisk.rtime, (uint64_t)tdisk.wtime, (uint64_t)tdisk.qtime);


    /* compute the values we actually want and add them to the dataptr */
    reads = (uint64_t)ceil((double)tdisk.reads/tdiff);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &reads, 1, OPAL_UINT64))) {
        return rc;
    }
    writes = (uint64_t)ceil((double)tdisk.writes/tdiff);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &writes, 1, OPAL_UINT64))) {
        return rc;
    }
    read_bytes = (uint64_t)ceil((double)tdisk.read_bytes/tdiff);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &read_bytes, 1, OPAL_UINT64))) {
        return rc;
    }
    write_bytes = (uint64_t)ceil((double)tdisk.write_bytes/tdiff);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &write_bytes, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &total_reads, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &total_writes, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &total_read_bytes, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &total_write_bytes, 1, OPAL_UINT64))) {
        return rc;
    }
    read_time = (uint64_t)ceil((double)tdisk.rtime);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &read_time, 1, OPAL_UINT64))) {
        return rc;
    }
    write_time = (uint64_t)ceil((double)tdisk.wtime);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &write_time, 1, OPAL_UINT64))) {
        return rc;
    }
    io_time = (uint64_t)ceil((double)tdisk.qtime);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &io_time, 1, OPAL_UINT64))) {
        return rc;
    }
    return ORCM_SUCCESS;
}

static int sigar_collect_network(opal_buffer_t *dataptr, double tdiff)
{
    int rc;
    sensor_sigar_interface_t *sit, *next;
    sigar_net_interface_stat_t tnet, ifc;
    uint64_t rxpkts, txpkts, rxbytes, txbytes;
    uint64_t total_mbytes_sent = 0, total_mbytes_recv = 0, total_packets_sent = 0, total_packets_recv = 0;
    uint64_t total_errors_sent = 0, total_errors_recv = 0;
    char *error_string;
    bool log_group = true;

    /* get network usage data */
    memset(&tnet, 0, sizeof(tnet));
    OPAL_LIST_FOREACH_SAFE(sit, next, &netlist, sensor_sigar_interface_t) {
        memset(&ifc, 0, sizeof(ifc));
        if (SIGAR_OK != (rc = sigar_net_interface_stat_get(sigar, sit->interface, &ifc))) {
            error_string = strerror(rc);
            opal_output_verbose(1 , orcm_sensor_base_framework.framework_output,
                                "sigar_net_interface_stat_get failed: %s", error_string);
            opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                                "%s Failed to get usage data for interface %s",
                                ORTE_NAME_PRINT(ORTE_PROC_MY_NAME), sit->interface);
            /* if we failed to get stats on this interface, remove from the list
             * so that we stop trying in the future as well */
            opal_list_remove_item(&netlist, &sit->super);
            OBJ_RELEASE(sit);
            log_group = false;
        } else {
            opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                                "Interface: %s RecvdPackets: %" PRIu64 " RecvdBytes: %" PRIu64 " TransPackets: %" PRIu64 " TransBytes: %" PRIu64 "",
                                sit->interface, (uint64_t)ifc.rx_packets, (uint64_t)ifc.rx_bytes, (uint64_t)ifc.tx_packets, (uint64_t)ifc.tx_bytes);
            /* compute the number of recvd packets since last reading */
            rxpkts = metric_diff_calc(ifc.rx_packets, sit->rx_packets, sit->interface, "rx packets");
            sit->rx_packets = ifc.rx_packets;  /* old = new */
            /* compute the number of transmitted packets since last reading */
            txpkts = metric_diff_calc(ifc.tx_packets, sit->tx_packets, sit->interface, "tx packets");
            sit->tx_packets = ifc.tx_packets;  /* old = new */
            /* compute the number of recvd bytes since last reading */
            rxbytes = metric_diff_calc(ifc.rx_bytes, sit->rx_bytes, sit->interface, "rx bytes");
            sit->rx_bytes = ifc.rx_bytes;  /* old = new */
            /* compute the number of transmitted bytes since last reading */
            txbytes = metric_diff_calc(ifc.tx_bytes, sit->tx_bytes, sit->interface, "tx bytes");
            sit->tx_bytes = ifc.tx_bytes;  /* old = new */
            opal_output_verbose(4, orcm_sensor_base_framework.framework_output,
                                "Interface: %s RxPkts: %" PRIu64 " TxPkts: %" PRIu64 " RxBytes: %" PRIu64 " TxBytes: %" PRIu64 "",
                                sit->interface, (uint64_t)rxpkts, (uint64_t)txpkts, (uint64_t)rxbytes, (uint64_t)txbytes);
            /* accumulate the values */
            tnet.rx_packets += rxpkts;
            tnet.rx_bytes += rxbytes;
            tnet.tx_packets += txpkts;
            tnet.tx_bytes += txbytes;
            total_mbytes_sent += ifc.tx_bytes;
            total_mbytes_recv += ifc.rx_bytes;
            total_packets_sent += ifc.tx_packets;
            total_packets_recv += ifc.rx_packets;
            total_errors_sent += ifc.tx_errors;
            total_errors_recv += ifc.rx_errors;
        }
    }
    opal_output_verbose(4, orcm_sensor_base_framework.framework_output,
                        "Totals: RxPkts: %" PRIu64 " TxPkts: %" PRIu64 " RxBytes: %" PRIu64 " TxBytes: %" PRIu64 "",
                        (uint64_t)tnet.rx_packets, (uint64_t)tnet.tx_packets, (uint64_t)tnet.rx_bytes, (uint64_t)tnet.tx_bytes);

    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &log_group, 1, OPAL_BOOL))) {
        return rc;
    }
    if (false == log_group)
        return ORCM_ERR_SENSOR_READ_FAIL;

    /* compute the values we actually want and add them to the data */
    rxpkts = (uint64_t)ceil((double)tnet.rx_packets/tdiff);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &rxpkts, 1, OPAL_UINT64))) {
        return rc;
    }
    txpkts = (uint64_t)ceil((double)tnet.tx_packets/tdiff);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &txpkts, 1, OPAL_UINT64))) {
        return rc;
    }
    rxbytes = (uint64_t)ceil((double)tnet.rx_bytes/tdiff);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &rxbytes, 1, OPAL_UINT64))) {
        return rc;
    }
    txbytes = (uint64_t)ceil((double)tnet.tx_bytes/tdiff);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &txbytes, 1, OPAL_UINT64))) {
        return rc;
    }
    total_mbytes_sent = (uint64_t)ceil((double)total_mbytes_sent/(1024*1024)); /* convert to Mbytes */
    total_mbytes_recv = (uint64_t)ceil((double)total_mbytes_recv/(1024*1024)); /* convert to Mbytes */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &total_mbytes_sent, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &total_mbytes_recv, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &total_packets_sent, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &total_packets_recv, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &total_errors_sent, 1, OPAL_UINT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &total_errors_recv, 1, OPAL_UINT64))) {
        return rc;
    }
    return ORCM_SUCCESS;
}

static int sigar_collect_system(opal_buffer_t *dataptr)
{
    int rc;
    sigar_uptime_t utime;
    char *error_string;
    bool log_group = true;

    /* get load average data */
    memset(&utime, 0, sizeof(utime));
    if (SIGAR_OK != (rc = sigar_uptime_get(sigar, &utime))) {
        error_string = strerror(rc);
        opal_output(0, "sigar_uptime_get failed: %s", error_string);
        log_group = false;
    }
    opal_output_verbose(1, orcm_sensor_base_framework.framework_output,
                        "uptime: %f", utime.uptime);

    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &log_group, 1, OPAL_BOOL))) {
        return rc;
    }
    if (false == log_group)
        return ORCM_ERR_SENSOR_READ_FAIL;
    /* add them to the dataptr */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &utime.uptime, 1, OPAL_DOUBLE))) {
        return rc;
    }
    return ORCM_SUCCESS;
}

static int sigar_collect_global_procstat(opal_buffer_t *dataptr)
{
    int rc;
    sigar_proc_stat_t       procstat_info;
    char *error_string;
    bool log_group = true;

    /* get load average data */
    memset(&procstat_info, 0, sizeof(procstat_info));
    if (SIGAR_OK != (rc = sigar_proc_stat_get(sigar, &procstat_info))) {
        error_string = strerror(rc);
        opal_output(0, "sigar_loadavg_get failed: %s", error_string);
        log_group = false;
    }

    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &log_group, 1, OPAL_BOOL))) {
        return rc;
    }
    if (false == log_group)
        return ORCM_ERR_SENSOR_READ_FAIL;


    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &procstat_info.total, 1, OPAL_INT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &procstat_info.sleeping, 1, OPAL_INT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &procstat_info.running, 1, OPAL_INT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &procstat_info.zombie, 1, OPAL_INT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &procstat_info.stopped, 1, OPAL_INT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &procstat_info.idle, 1, OPAL_INT64))) {
        return rc;
    }
    if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &procstat_info.threads, 1, OPAL_INT64))) {
        return rc;
    }

    return ORCM_SUCCESS;
}

static int sigar_collect_procstat(opal_buffer_t *dataptr)
{
    int rc;
    sigar_proc_state_t proc_info;
    sigar_proc_mem_t   proc_mem_info;
    sigar_proc_cpu_t   proc_cpu_info;
    char *error_string;
    bool log_group = true;
    opal_pstats_t *stats;
    pid_t sample_pid;
    int i,remaining_procs;
    orte_proc_t *child;

    if (NULL != orte_local_children) {
        remaining_procs = orte_local_children->size+1;
    } else {
        remaining_procs = 1;
    }

    for (i=0; i < remaining_procs; i++) {
        if(0==i)
        {
            sample_pid = orte_process_info.pid;
        } else {
            if (NULL == (child = (orte_proc_t*)opal_pointer_array_get_item(orte_local_children, i))) {
                continue;
            }
            if (!ORTE_FLAG_TEST(child, ORTE_PROC_FLAG_ALIVE)) {
                continue;
            }
            if (0 == child->pid) {
                /* race condition */
                continue;
            }
            sample_pid = child->pid;
        }

        /* reset procstat data */
        memset(&proc_info, 0, sizeof(proc_info));
        memset(&proc_mem_info, 0, sizeof(proc_mem_info));
        memset(&proc_cpu_info, 0, sizeof(proc_cpu_info));

        /* Sample all the proc details here */
        if (SIGAR_OK != (rc = sigar_proc_state_get(sigar, sample_pid, &proc_info))) {
            error_string = strerror(rc);
            opal_output(0, "sigar_proc_state_get failed: %s", error_string);
            log_group = false;
        }
        if (SIGAR_OK != (rc = sigar_proc_mem_get(sigar, sample_pid, &proc_mem_info))) {
            error_string = strerror(rc);
            opal_output(0, "sigar_proc_mem_get failed: %s", error_string);
            log_group = false;
        }
        if (SIGAR_OK != (rc = sigar_proc_cpu_get(sigar, sample_pid, &proc_cpu_info))) {
            error_string = strerror(rc);
            opal_output(0, "sigar_loadavg_get failed: %s", error_string);
            log_group = false;
        }
        if (false == log_group)
            continue;

        stats = OBJ_NEW(opal_pstats_t);

        stats->pid = orte_process_info.pid;
        strncpy(stats->cmd, proc_info.name, sizeof(stats->cmd)-1);
        stats->cmd[sizeof(stats->cmd)-1] = '\0';
        stats->state[0] = proc_info.state;
        stats->state[1] = '\0';
        stats->priority = proc_info.priority;
        stats->num_threads = proc_info.threads;
        stats->vsize = proc_mem_info.size;
        stats->rss = proc_mem_info.resident;
        stats->processor = proc_info.processor;
        stats->rank = 0;

        log_group = true;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &log_group, 1, OPAL_BOOL))) {
            OBJ_DESTRUCT(stats);
            return rc;
        }

        if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &stats, 1, OPAL_PSTAT))) {
            OBJ_DESTRUCT(stats);
            return rc;
        }
        OBJ_DESTRUCT(stats);

        if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &proc_mem_info.share, 1, OPAL_INT64))) {
            return rc;
        }
        if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &proc_mem_info.minor_faults, 1, OPAL_INT64))) {
            return rc;
        }
        if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &proc_mem_info.major_faults, 1, OPAL_INT64))) {
            return rc;
        }
        if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &proc_mem_info.page_faults, 1, OPAL_INT64))) {
            return rc;
        }
        if (OPAL_SUCCESS != (rc = opal_dss.pack(dataptr, &proc_cpu_info.percent, 1, OPAL_DOUBLE))) {
            return rc;
        }
    }

    return ORCM_SUCCESS;
}

static void sigar_sample(orcm_sensor_sampler_t *sampler)
{
    opal_output_verbose(5, orcm_sensor_base_framework.framework_output,
                            "%s sensor sigar : sigar_sample: called",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));
    if (!mca_sensor_sigar_component.use_progress_thread) {
       collect_sample(sampler);
    }

}

static void perthread_sigar_sample(int fd, short args, void *cbdata)
{
    orcm_sensor_sampler_t *sampler = (orcm_sensor_sampler_t*)cbdata;

    opal_output_verbose(5, orcm_sensor_base_framework.framework_output,
                            "%s sensor sigar : perthread_sigar_sample: called",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME));

    /* this has fired in the sampler thread, so we are okay to
     * just go ahead and sample since we do NOT allow both the
     * base thread and the component thread to both be actively
     * calling this component */
    collect_sample(sampler);
    /* we now need to push the results into the base event thread
     * so it can add the data to the base bucket */
    ORCM_SENSOR_XFER(&sampler->bucket);
    /* clear the bucket */
    OBJ_DESTRUCT(&sampler->bucket);
    OBJ_CONSTRUCT(&sampler->bucket, opal_buffer_t);
    /* check if sigar sample rate is provided for this*/
    if (mca_sensor_sigar_component.sample_rate != sampler->rate.tv_sec) {
        sampler->rate.tv_sec = mca_sensor_sigar_component.sample_rate;
    }
    /* set ourselves to sample again */
    opal_event_evtimer_add(&sampler->ev, &sampler->rate);
}

static void collect_sample(orcm_sensor_sampler_t *sampler)
{
    opal_buffer_t data, *bptr;
    int rc;
    time_t now;
    double tdiff;
    char *ctmp;
    bool log_group=false;
    struct timeval current_time;

    if (mca_sensor_sigar_component.test) {
        /* just send the test vector */
        bptr = &test_vector;
        opal_dss.pack(&sampler->bucket, &bptr, 1, OPAL_BUFFER);
        return;
    }

    /* prep the buffer to collect the data */
    OBJ_CONSTRUCT(&data, opal_buffer_t);
    /* pack our name */
    ctmp = strdup("sigar");
    if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &ctmp, 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        OBJ_DESTRUCT(&data);
        return;
    }
    free(ctmp);
    /* include our node name */
    if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &orte_process_info.nodename, 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        OBJ_DESTRUCT(&data);
        return;
    }

    /* get the sample time */
    now = time(NULL);
    tdiff = difftime(now, last_sample);
    /* pass the time along as a simple string */
    gettimeofday(&current_time, NULL);

    if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &current_time, 1, OPAL_TIMEVAL))) {
        ORTE_ERROR_LOG(rc);
        OBJ_DESTRUCT(&data);
        return;
    }

    /* 1. Memory stats*/
    if(mca_sensor_sigar_component.mem) {
        if(ORCM_ERR_SENSOR_READ_FAIL == (rc = sigar_collect_mem(&data))) {
            ORTE_ERROR_LOG(rc);
        } else if (ORCM_SUCCESS != rc) {
            OBJ_DESTRUCT(&data);
            ORTE_ERROR_LOG(rc);
            return;
        }
    } else {
        log_group = false;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &log_group, 1, OPAL_BOOL))) {
            ORTE_ERROR_LOG(rc);
            OBJ_DESTRUCT(&data);
            return;
        }
    }

    /* 2. Swap Memory stats */
    if(mca_sensor_sigar_component.swap) {
        if(ORCM_ERR_SENSOR_READ_FAIL == (rc = sigar_collect_swap(&data))) {
            ORTE_ERROR_LOG(rc);
        } else if (ORCM_SUCCESS != rc) {
            OBJ_DESTRUCT(&data);
            ORTE_ERROR_LOG(rc);
            return;
        }
    } else {
        log_group = false;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &log_group, 1, OPAL_BOOL))) {
            ORTE_ERROR_LOG(rc);
            OBJ_DESTRUCT(&data);
            return;
        }
    }

    /* 3. CPU stats*/
    if(mca_sensor_sigar_component.cpu) {
        if(ORCM_ERR_SENSOR_READ_FAIL == (rc = sigar_collect_cpu(&data))) {
            ORTE_ERROR_LOG(rc);
        } else if (ORCM_SUCCESS != rc) {
            OBJ_DESTRUCT(&data);
            ORTE_ERROR_LOG(rc);
            return;
        }
    } else {
        log_group = false;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &log_group, 1, OPAL_BOOL))) {
            ORTE_ERROR_LOG(rc);
            OBJ_DESTRUCT(&data);
            return;
        }
    }

    /* 4. load stats */
    if(mca_sensor_sigar_component.load) {
        if(ORCM_ERR_SENSOR_READ_FAIL == (rc = sigar_collect_load(&data))) {
            ORTE_ERROR_LOG(rc);
        } else if (ORCM_SUCCESS != rc) {
            OBJ_DESTRUCT(&data);
            ORTE_ERROR_LOG(rc);
            return;
        }
    } else {
        log_group = false;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &log_group, 1, OPAL_BOOL))) {
            ORTE_ERROR_LOG(rc);
            OBJ_DESTRUCT(&data);
            return;
        }
    }

    /* 5. disk stats */
    if(mca_sensor_sigar_component.disk) {
        if(ORCM_ERR_SENSOR_READ_FAIL == (rc = sigar_collect_disk(&data, tdiff))) {
            ORTE_ERROR_LOG(rc);
        } else if (ORCM_SUCCESS != rc) {
            OBJ_DESTRUCT(&data);
            ORTE_ERROR_LOG(rc);
            return;
        }
    } else {
        log_group = false;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &log_group, 1, OPAL_BOOL))) {
            ORTE_ERROR_LOG(rc);
            OBJ_DESTRUCT(&data);
            return;
        }
    }

    /* 6. network stats */
    if(mca_sensor_sigar_component.network) {
        if(ORCM_ERR_SENSOR_READ_FAIL == (rc = sigar_collect_network(&data, tdiff))) {
            ORTE_ERROR_LOG(rc);
        } else if (ORCM_SUCCESS != rc) {
            OBJ_DESTRUCT(&data);
            ORTE_ERROR_LOG(rc);
            return;
        }
    } else {
        log_group = false;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &log_group, 1, OPAL_BOOL))) {
            ORTE_ERROR_LOG(rc);
            OBJ_DESTRUCT(&data);
            return;
        }
    }

    /* 7. System stats */
    if(mca_sensor_sigar_component.sys) {
        if(ORCM_ERR_SENSOR_READ_FAIL == (rc = sigar_collect_system(&data))) {
            ORTE_ERROR_LOG(rc);
        } else if (ORCM_SUCCESS != rc) {
            OBJ_DESTRUCT(&data);
            ORTE_ERROR_LOG(rc);
            return;
        }
    } else {
        log_group = false;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &log_group, 1, OPAL_BOOL))) {
            ORTE_ERROR_LOG(rc);
            OBJ_DESTRUCT(&data);
            return;
        }
    }

    /* 8. proc stats */
    if(mca_sensor_sigar_component.proc) {
        if(ORCM_ERR_SENSOR_READ_FAIL == (rc = sigar_collect_global_procstat(&data))) {
            ORTE_ERROR_LOG(rc);
        } else if (ORCM_SUCCESS != rc) {
            OBJ_DESTRUCT(&data);
            ORTE_ERROR_LOG(rc);
            return;
        }
        if(ORCM_ERR_SENSOR_READ_FAIL == (rc = sigar_collect_procstat(&data))) {
            ORTE_ERROR_LOG(rc);
        } else if (ORCM_SUCCESS != rc) {
            OBJ_DESTRUCT(&data);
            ORTE_ERROR_LOG(rc);
            return;
        }
    } else {
        log_group = false;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &log_group, 1, OPAL_BOOL))) {
            ORTE_ERROR_LOG(rc);
            OBJ_DESTRUCT(&data);
            return;
        }
    }

    /* No More process stats to pack - So pack the final marker*/
    log_group = false;
    if (OPAL_SUCCESS != (rc = opal_dss.pack(&data, &log_group, 1, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        OBJ_DESTRUCT(&data);
        return;
    }

    /* xfer the data for transmission - need at least one prior sample before doing so */
    if (0 < last_sample) {
        bptr = &data;
        if (OPAL_SUCCESS != (rc = opal_dss.pack(&sampler->bucket, &bptr, 1, OPAL_BUFFER))) {
            ORTE_ERROR_LOG(rc);
            OBJ_DESTRUCT(&data);
            return;
        }
    }
    OBJ_DESTRUCT(&data);
    last_sample = now;
}

static void mycleanup(int dbhandle, int status, opal_list_t *kvs,
                      opal_list_t *ret, void *cbdata)
{
    OPAL_LIST_RELEASE(kvs);
    if (ORTE_SUCCESS != status) {
        log_enabled = false;
    }
}

static void mycleanup_procstat(int dbhandle, int status, opal_list_t *kvs,
                               opal_list_t *ret, void *cbdata)
{
    OPAL_LIST_RELEASE(kvs);
    if (ORTE_SUCCESS != status) {
        log_enabled = false;
    }
    if(NULL != cbdata)
        free(cbdata);
}

static void sigar_log(opal_buffer_t *sample)
{
    char *hostname;
    int rc;
    int32_t n;
    opal_list_t *vals;
    opal_value_t *kv;
    uint64_t uint64;
    int64_t int64;
    float fval;
    bool log_group = false, data_avail = false;
    double sample_double;
    char *primary_key;
    opal_pstats_t *st;
    struct timeval sampletime;
    orcm_metric_value_t *sensor_metric;

    if (!log_enabled) {
        return;
    }


    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &hostname, &n, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        return;
    }

    opal_output_verbose(3, orcm_sensor_base_framework.framework_output,
                        "%s Received log from host %s",
                        ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                        (NULL == hostname) ? "NULL" : hostname);

    /* prep the xfr storage */
    vals = OBJ_NEW(opal_list_t);

    /* sample time */
    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &sampletime, &n, OPAL_TIMEVAL))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }

    kv = OBJ_NEW(opal_value_t);
    kv->key = strdup("ctime");
    kv->type = OPAL_TIMEVAL;
    kv->data.tv = sampletime;
    opal_list_append(vals, &kv->super);

    /* load the hostname */
    if (NULL == hostname) {
        ORTE_ERROR_LOG(OPAL_ERR_BAD_PARAM);
        return;
    }
    kv = OBJ_NEW(opal_value_t);
    kv->key = strdup("hostname");
    kv->type = OPAL_STRING;
    kv->data.string = strdup(hostname);
    opal_list_append(vals, &kv->super);

    kv = OBJ_NEW(opal_value_t);
    if (NULL == kv) {
        ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
        goto cleanup;
    }
    kv->key = strdup("data_group");
    kv->type = OPAL_STRING;
    kv->data.string = strdup("sigar");
    opal_list_append(vals, &kv->super);

    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &log_group, &n, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }

    if(log_group) {
        data_avail = true;
        /* total memory */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("mem_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("Bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* total used memory */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("mem_used");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("Bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* actual used memory */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("mem_actual_used");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("Bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* actual free memory */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("mem_actual_free");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("Bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);
    }

    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &log_group, &n, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }

    if(log_group) {
        data_avail = true;
        /* total swap memory */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("swap_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("Bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* swap used */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("swap_used");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("Bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* swap pages in */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("swap_page_in");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("Bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* swap pages out */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("swap_page_out");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("Bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);
    }

    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &log_group, &n, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }

    if(log_group) {
        data_avail = true;
        /* cpu user */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &fval, &n, OPAL_FLOAT))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("cpu_user");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = fval;
        sensor_metric->units = strdup("%");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* cpu sys */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &fval, &n, OPAL_FLOAT))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("cpu_sys");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = fval;
        sensor_metric->units = strdup("%");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* cpu idle */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &fval, &n, OPAL_FLOAT))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("cpu_idle");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = fval;
        sensor_metric->units = strdup("%");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);
    }

    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &log_group, &n, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }

    if(log_group) {
        data_avail = true;
        /* la0 */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &fval, &n, OPAL_FLOAT))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("load0");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = fval;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* la5 */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &fval, &n, OPAL_FLOAT))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("load1");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = fval;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* la15 */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &fval, &n, OPAL_FLOAT))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("load2");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = fval;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);
    }
    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &log_group, &n, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }

    if(log_group) {
        data_avail = true;
        /* disk read ops rate */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("disk_ro_rate");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("ops/sec");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* disk write ops rate */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            return;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("disk_wo_rate");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("ops/sec");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* disk read bytes/sec */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("disk_rb_rate");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("bytes/sec");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* disk write bytes/sec */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("disk_wb_rate");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("bytes/sec");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* disk Total Read Ops count */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("disk_ro_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("ops");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* disk Total Write Ops count */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("disk_wo_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("ops");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* disk Total Read Bytes count */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("disk_rb_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* disk Total Write Bytes count */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("disk_wb_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* disk Total Read Ops Time duration */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("disk_rt_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("msec");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* disk Total Write Ops Time count */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("disk_wt_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("msec");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* disk Total io Ops Time count */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("disk_iot_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("msec");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);
    }
    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &log_group, &n, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }

    if(log_group) {
        data_avail = true;
        /* net recv packet rate */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("net_rp_rate");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("packets/sec");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* net tx packet rate */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("net_wp_rate");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("packets/sec");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* net recv bytes rate */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("net_rb_rate");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("bytes/sec");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* net tx bytes rate */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("net_wb_rate");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("bytes/sec");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* net tx bytes total */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            return;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("net_wb_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("Mbytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* net rx bytes total */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("net_rb_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("Mbytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* net tx packets total */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            return;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("net_wp_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("packets");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* net rx packets total */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            return;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("net_rp_total");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("packets");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* net tx errors total */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            return;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("net_tx_errors");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("errors");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* net rx errors total */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &uint64, &n, OPAL_UINT64))) {
            ORTE_ERROR_LOG(rc);
            return;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("net_rx_errors");
        sensor_metric->value.type = OPAL_UINT64;
        sensor_metric->value.data.uint64 = uint64;
        sensor_metric->units = strdup("errors");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);
    }

    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &log_group, &n, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        return;
    }

    if(log_group) {
        data_avail = true;
        /* process System Uptime */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &sample_double, &n, OPAL_DOUBLE))) {
            ORTE_ERROR_LOG(rc);
            return;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("uptime");
        sensor_metric->value.type = OPAL_DOUBLE;
        sensor_metric->value.data.dval = sample_double;
        sensor_metric->units = strdup("seconds");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

    }
    /* store it */
    if ((0 <= orcm_sensor_base.dbhandle) & (true == data_avail)) {
        orcm_db.store_new(orcm_sensor_base.dbhandle, ORCM_DB_ENV_DATA, vals, NULL, mycleanup, NULL);
    } else {
        OPAL_LIST_RELEASE(vals);
    }

    /* Unpack the collected process stats and store them in the database */
    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &log_group, &n, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        return;
    }

    if(log_group) {
        vals = OBJ_NEW(opal_list_t);

        kv = OBJ_NEW(opal_value_t);
        kv->key = strdup("ctime");
        kv->type = OPAL_TIMEVAL;
        kv->data.tv = sampletime;
        opal_list_append(vals, &kv->super);

        /* load the hostname */
        if (NULL == hostname) {
            ORTE_ERROR_LOG(OPAL_ERR_BAD_PARAM);
            return;
        }
        kv = OBJ_NEW(opal_value_t);
        kv->key = strdup("hostname");
        kv->type = OPAL_STRING;
        kv->data.string = strdup(hostname);
        opal_list_append(vals, &kv->super);

        kv = OBJ_NEW(opal_value_t);
        if (NULL == kv) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        kv->key = strdup("data_group");
        kv->type = OPAL_STRING;
        kv->data.string = strdup("procstat");
        opal_list_append(vals, &kv->super);

        /* process threads */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &int64, &n, OPAL_INT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("total_processes");
        sensor_metric->value.type = OPAL_INT64;
        sensor_metric->value.data.int64 = int64;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* process threads */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &int64, &n, OPAL_INT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("sleeping_processes");
        sensor_metric->value.type = OPAL_INT64;
        sensor_metric->value.data.int64 = int64;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* process threads */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &int64, &n, OPAL_INT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("running_processes");
        sensor_metric->value.type = OPAL_INT64;
        sensor_metric->value.data.int64 = int64;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);
        /* process threads */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &int64, &n, OPAL_INT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("zombie_processes");
        sensor_metric->value.type = OPAL_INT64;
        sensor_metric->value.data.int64 = int64;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* process threads */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &int64, &n, OPAL_INT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("stopped_processes");
        sensor_metric->value.type = OPAL_INT64;
        sensor_metric->value.data.int64 = int64;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* process threads */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &int64, &n, OPAL_INT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("idle_processes");
        sensor_metric->value.type = OPAL_INT64;
        sensor_metric->value.data.int64 = int64;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* process threads */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &int64, &n, OPAL_INT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("total_threads");
        sensor_metric->value.type = OPAL_INT64;
        sensor_metric->value.data.int64 = int64;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* store it */
        if (0 <= orcm_sensor_base.dbhandle) {
            orcm_db.store_new(orcm_sensor_base.dbhandle, ORCM_DB_ENV_DATA, vals, NULL, mycleanup, NULL);
        } else {
            OPAL_LIST_RELEASE(vals);
        }
    }
    /* Check if any process level stats are being sent */
    n=1;
    if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &log_group, &n, OPAL_BOOL))) {
        ORTE_ERROR_LOG(rc);
        goto cleanup;
    }
    while(true == log_group) {
        vals = OBJ_NEW(opal_list_t);

        kv = OBJ_NEW(opal_value_t);
        kv->key = strdup("ctime");
        kv->type = OPAL_TIMEVAL;
        kv->data.tv = sampletime;
        opal_list_append(vals, &kv->super);

        /* load the hostname */
        if (NULL == hostname) {
            ORTE_ERROR_LOG(OPAL_ERR_BAD_PARAM);
            goto cleanup;
        }
        kv = OBJ_NEW(opal_value_t);
        kv->key = strdup("hostname");
        kv->type = OPAL_STRING;
        kv->data.string = strdup(hostname);
        opal_list_append(vals, &kv->super);

        /* process id */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &st, &n, OPAL_PSTAT))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        /* Do not free primary_key! It will be freed in mycleanup_procstat */
        asprintf(&primary_key, "procstat_%s",st->cmd);

        kv = OBJ_NEW(opal_value_t);
        if (NULL == kv) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        kv->key = strdup("data_group");
        kv->type = OPAL_STRING;
	if (NULL == primary_key){
		return;
	}
	else {
        kv->data.string = strdup(primary_key);
	}
        opal_list_append(vals, &kv->super);


        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("pid");
        sensor_metric->value.type = OPAL_PID;
        sensor_metric->value.data.pid = st->pid;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("cmd");
        sensor_metric->value.type = OPAL_STRING;
        sensor_metric->value.data.string = strdup(st->cmd);
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("state");
        sensor_metric->value.type = OPAL_STRING;
        sensor_metric->value.data.string = (char*)malloc(3 * sizeof(char));
        if (NULL == sensor_metric->value.data.string) {
            goto cleanup;
        }
        sensor_metric->value.data.string[0] = st->state[0];
        sensor_metric->value.data.string[1] = st->state[1];
        sensor_metric->value.data.string[2] = '\0';
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("percent_cpu");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = st->percent_cpu;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("priority");
        sensor_metric->value.type = OPAL_INT32;
        sensor_metric->value.data.int32 = st->priority;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("num_threads");
        sensor_metric->value.type = OPAL_INT16;
        sensor_metric->value.data.int16 = st->num_threads;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("vsize");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = st->vsize;
        sensor_metric->units = strdup("Bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("rss");
        sensor_metric->value.type = OPAL_FLOAT;
        sensor_metric->value.data.fval = st->rss;
        sensor_metric->units = strdup("Bytes");
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("processor");
        sensor_metric->value.type = OPAL_INT16;
        sensor_metric->value.data.int16 = st->processor;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* process Shared Memory */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &int64, &n, OPAL_INT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("shared memory");
        sensor_metric->value.type = OPAL_INT64;
        sensor_metric->value.data.int64 = int64;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* process minor_faults */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &int64, &n, OPAL_INT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("minor_faults");
        sensor_metric->value.type = OPAL_INT64;
        sensor_metric->value.data.int64 = int64;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* process major_faults */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &int64, &n, OPAL_INT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("major_faults");
        sensor_metric->value.type = OPAL_INT64;
        sensor_metric->value.data.int64 = int64;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* process total_page_faults */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &int64, &n, OPAL_INT64))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("page_faults");
        sensor_metric->value.type = OPAL_INT64;
        sensor_metric->value.data.int64 = int64;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

        /* process cpu percent */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &sample_double, &n, OPAL_DOUBLE))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        sensor_metric = OBJ_NEW(orcm_metric_value_t);
        if (NULL == sensor_metric) {
            ORTE_ERROR_LOG(OPAL_ERR_OUT_OF_RESOURCE);
            goto cleanup;
        }
        sensor_metric->value.key = strdup("percent");
        sensor_metric->value.type = OPAL_DOUBLE;
        sensor_metric->value.data.dval = sample_double;
        sensor_metric->units = NULL;
        opal_list_append(vals, (opal_list_item_t *)sensor_metric);

       /* store it */
        if (0 <= orcm_sensor_base.dbhandle) {
            orcm_db.store_new(orcm_sensor_base.dbhandle, ORCM_DB_ENV_DATA, vals, NULL, mycleanup_procstat, primary_key);
        } else {
            OPAL_LIST_RELEASE(vals);
            if(primary_key!=NULL) {
                free(primary_key);
            }
        }

        /* process cpu percent */
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(sample, &log_group, &n, OPAL_BOOL))) {
            ORTE_ERROR_LOG(rc);
            goto cleanup;
        }
        OBJ_RELEASE(st);
    }
    goto done;

cleanup:
    if (NULL != primary_key) {
        free(primary_key);
    }
done:
    if (NULL != hostname) {
        free(hostname);
    }
}

/* Helper function to calculate the metric differences */
static uint64_t metric_diff_calc(sigar_uint64_t newval, uint64_t oldval,
                                 const char *name_for_log,
                                 const char *value_name_for_log)
{
    uint64_t diff;

    if (newval < oldval) {
        /* assume that the value was reset and we are starting over */
        opal_output_verbose(3, orcm_sensor_base_framework.framework_output,
                            "%s metric_diff_calc: new value %" PRIu64 " is less than old value %" PRIu64
                            " for %s metric %s; assume the value was reset and set diff to new value.",
                            ORTE_NAME_PRINT(ORTE_PROC_MY_NAME),
                            (uint64_t)newval, (uint64_t)oldval, name_for_log, value_name_for_log);
        diff = newval;
    } else {
        diff = newval - oldval;
    }

    return diff;
}

static void generate_test_vector(opal_buffer_t *v)
{
    char *ctmp, *date;
    uint64_t ui64;
    float ft;
    time_t now;

    ctmp = strdup("sigar");
    opal_dss.pack(v, &ctmp, 1, OPAL_STRING);
    free(ctmp);
    opal_dss.pack(v, &orte_process_info.nodename, 1, OPAL_STRING);
    /* get the time so it will be unique each time */
    now = time(NULL);
    /* pass the time along as a simple string */
    date = ctime(&now);
    if(NULL != date && 0 != strlen(date)) {
        /* strip the trailing newline */
        date[strlen(date)-1] = '\0';
    }
    opal_dss.pack(v, &date, 1, OPAL_STRING);
    /* mem_total */
    ui64 = 1;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* mem_used */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* mem_actual_used */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* mem_actual_free */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* swap total */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* swap used */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* swap page in */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* swap page out */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* cpu user */
    ft = 1.0;
    opal_dss.pack(v, &ft, 1, OPAL_FLOAT);
    /* cpu sys */
    ft += 1.0;
    opal_dss.pack(v, &ft, 1, OPAL_FLOAT);
    /* cpu idle */
    ft += 1.0;
    opal_dss.pack(v, &ft, 1, OPAL_FLOAT);
    /* la */
    ft += 1.0;
    opal_dss.pack(v, &ft, 1, OPAL_FLOAT);
    /* la5 */
    ft += 1.0;
    opal_dss.pack(v, &ft, 1, OPAL_FLOAT);
    /* la15 */
    ft += 1.0;
    opal_dss.pack(v, &ft, 1, OPAL_FLOAT);
    /* reads */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* writes */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* read bytes */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* write bytes */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* rx packets */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* tx packets */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* rx bytes */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
    /* tx bytes */
    ui64++;
    opal_dss.pack(v, &ui64, 1, OPAL_UINT64);
}

static void sigar_set_sample_rate(int sample_rate)
{
    /* set the sigar sample rate if seperate thread is enabled */
    if (mca_sensor_sigar_component.use_progress_thread) {
        mca_sensor_sigar_component.sample_rate = sample_rate;
    }
    return;
}

static void sigar_get_sample_rate(int *sample_rate)
{
    if (NULL != sample_rate) {
    /* check if sigar sample rate is provided for this*/
            *sample_rate = mca_sensor_sigar_component.sample_rate;
    }
    return;
}

static void sigar_inventory_collect(opal_buffer_t *inventory_snapshot)
{
    static char* sensor_names[] = {
      /* primary_key=sigar (36) */
        "mem_total",
        "mem_used",
        "mem_actual_used",
        "mem_actual_free",
        "swap_total",
        "swap_used",
        "swap_page_in",
        "swap_page_out",
        "cpu_user",
        "cpu_sys",
        "cpu_idle",
        "load0",
        "load1",
        "load2",
        "disk_ro_rate",
        "disk_wo_rate",
        "disk_rb_rate",
        "disk_wb_rate",
        "disk_ro_total",
        "disk_wo_total",
        "disk_rb_total",
        "disk_wb_total",
        "disk_rt_total",
        "disk_wt_total",
        "disk_iot_total",
        "net_rp_rate",
        "net_wp_rate",
        "net_rb_rate",
        "net_wb_rate",
        "net_wb_total",
        "net_rb_total",
        "net_wp_total",
        "net_rp_total",
        "net_tx_errors",
        "net_rx_errors",
        "uptime",
      /* primary_key=procstat (7) */
        "total_processes",
        "sleeping_processes",
        "running_processes",
        "zombie_processes",
        "stopped_processes",
        "idle_processes",
        "total_threads",
      /* primary_key=procstat_<process> (14) */
        "pid",
        "cmd",
        "state",
        "percent_cpu",
        "priority",
        "num_threads",
        "vsize",
        "rss",
        "processor",
        "shared memory",
        "minor_faults",
        "major_faults",
        "page_faults",
        "percent"
    };
    unsigned int tot_items = 58;
    unsigned int i = 0;
    char *comp = strdup("sigar");
    int rc = OPAL_SUCCESS;

    if (OPAL_SUCCESS != (rc = opal_dss.pack(inventory_snapshot, &comp, 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        free(comp);
        return;
    }
    free(comp);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(inventory_snapshot, &tot_items, 1, OPAL_UINT))) {
        ORTE_ERROR_LOG(rc);
        return;
    }
    --tot_items; /* adjust out "hostname"/nodename pair */

    /* store our hostname */
    comp = strdup("hostname");
    if (OPAL_SUCCESS != (rc = opal_dss.pack(inventory_snapshot, &comp, 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        free(comp);
        return;
    }
    free(comp);
    if (OPAL_SUCCESS != (rc = opal_dss.pack(inventory_snapshot, &orte_process_info.nodename, 1, OPAL_STRING))) {
        ORTE_ERROR_LOG(rc);
        return;
    }

    for(i = 0; i < tot_items; ++i) {
        asprintf(&comp, "sensor_sigar_%d", i+1);
        if (OPAL_SUCCESS != (rc = opal_dss.pack(inventory_snapshot, &comp, 1, OPAL_STRING))) {
            ORTE_ERROR_LOG(rc);
            free(comp);
            return;
        }
        free(comp);
        comp = strdup(sensor_names[i]);
        if (OPAL_SUCCESS != (rc = opal_dss.pack(inventory_snapshot, &comp, 1, OPAL_STRING))) {
            ORTE_ERROR_LOG(rc);
            free(comp);
            return;
        }
        free(comp);
    }
}

static void my_inventory_log_cleanup(int dbhandle, int status, opal_list_t *kvs, opal_list_t *output, void *cbdata)
{
    OBJ_RELEASE(kvs);
}

static void sigar_inventory_log(char *hostname, opal_buffer_t *inventory_snapshot)
{
    unsigned int tot_items = 0;
    int n = 1;
    opal_list_t *records = NULL;
    int rc = OPAL_SUCCESS;

    if (OPAL_SUCCESS != (rc = opal_dss.unpack(inventory_snapshot, &tot_items, &n, OPAL_UINT))) {
        ORTE_ERROR_LOG(rc);
        return;
    }
    records = OBJ_NEW(opal_list_t);
    while(tot_items > 0) {
        char *inv = NULL;
        char *inv_val = NULL;
        orcm_metric_value_t *mkv = NULL;

        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(inventory_snapshot, &inv, &n, OPAL_STRING))) {
            ORTE_ERROR_LOG(rc);
            OBJ_RELEASE(records);
            return;
        }
        n=1;
        if (OPAL_SUCCESS != (rc = opal_dss.unpack(inventory_snapshot, &inv_val, &n, OPAL_STRING))) {
            ORTE_ERROR_LOG(rc);
            OBJ_RELEASE(records);
            return;
        }

        mkv = OBJ_NEW(orcm_metric_value_t);
        mkv->value.key = inv;
        mkv->value.type = OPAL_STRING;
        mkv->value.data.string = inv_val;
        opal_list_append(records, (opal_list_item_t*)mkv);

        --tot_items;
    }
    if (0 <= orcm_sensor_base.dbhandle) {
        orcm_db.store_new(orcm_sensor_base.dbhandle, ORCM_DB_INVENTORY_DATA, records, NULL, my_inventory_log_cleanup, NULL);
    } else {
        my_inventory_log_cleanup(-1, -1, records, NULL, NULL);
    }
}
