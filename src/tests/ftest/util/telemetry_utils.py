#!/usr/bin/python
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
from ClusterShell.NodeSet import NodeSet


class TelemetryUtils():
    """Defines a object used to verify telemetry information."""

    ENGINE_EVENT_METRICS = [
        "engine_events_dead_ranks",
        "engine_events_last_event_ts",
        "engine_servicing_at",
        "engine_started_at"]
    ENGINE_IO_METRICS = [
        "engine_io_dtx_committable"
        "engine_io_dtx_committable_max"
        "engine_io_dtx_committable_mean"
        "engine_io_dtx_committable_min"
        "engine_io_dtx_committable_stddev"
        "engine_io_dtx_committed"
        "engine_io_dtx_committed_max"
        "engine_io_dtx_committed_mean"
        "engine_io_dtx_committed_min"
        "engine_io_dtx_committed_stddev"
        "engine_io_latency_fetch"
        "engine_io_latency_fetch_max"
        "engine_io_latency_fetch_mean"
        "engine_io_latency_fetch_min"
        "engine_io_latency_fetch_stddev"
        "engine_io_latency_update"
        "engine_io_latency_update_max"
        "engine_io_latency_update_mean"
        "engine_io_latency_update_min"
        "engine_io_latency_update_stddev"
        "engine_io_ops_akey_enum_active"
        "engine_io_ops_akey_enum_active_max"
        "engine_io_ops_akey_enum_active_mean"
        "engine_io_ops_akey_enum_active_min"
        "engine_io_ops_akey_enum_active_stddev"
        "engine_io_ops_akey_enum_latency"
        "engine_io_ops_akey_enum_latency_max"
        "engine_io_ops_akey_enum_latency_mean"
        "engine_io_ops_akey_enum_latency_min"
        "engine_io_ops_akey_enum_latency_stddev"
        "engine_io_ops_akey_punch_active"
        "engine_io_ops_akey_punch_active_max"
        "engine_io_ops_akey_punch_active_mean"
        "engine_io_ops_akey_punch_active_min"
        "engine_io_ops_akey_punch_active_stddev"
        "engine_io_ops_akey_punch_latency"
        "engine_io_ops_akey_punch_latency_max"
        "engine_io_ops_akey_punch_latency_mean"
        "engine_io_ops_akey_punch_latency_min"
        "engine_io_ops_akey_punch_latency_stddev"
        "engine_io_ops_compound_active"
        "engine_io_ops_compound_active_max"
        "engine_io_ops_compound_active_mean"
        "engine_io_ops_compound_active_min"
        "engine_io_ops_compound_active_stddev"
        "engine_io_ops_compound_latency"
        "engine_io_ops_compound_latency_max"
        "engine_io_ops_compound_latency_mean"
        "engine_io_ops_compound_latency_min"
        "engine_io_ops_compound_latency_stddev"
        "engine_io_ops_dkey_enum_active"
        "engine_io_ops_dkey_enum_active_max"
        "engine_io_ops_dkey_enum_active_mean"
        "engine_io_ops_dkey_enum_active_min"
        "engine_io_ops_dkey_enum_active_stddev"
        "engine_io_ops_dkey_enum_latency"
        "engine_io_ops_dkey_enum_latency_max"
        "engine_io_ops_dkey_enum_latency_mean"
        "engine_io_ops_dkey_enum_latency_min"
        "engine_io_ops_dkey_enum_latency_stddev"
        "engine_io_ops_dkey_punch_active"
        "engine_io_ops_dkey_punch_active_max"
        "engine_io_ops_dkey_punch_active_mean"
        "engine_io_ops_dkey_punch_active_min"
        "engine_io_ops_dkey_punch_active_stddev"
        "engine_io_ops_dkey_punch_latency"
        "engine_io_ops_dkey_punch_latency_max"
        "engine_io_ops_dkey_punch_latency_mean"
        "engine_io_ops_dkey_punch_latency_min"
        "engine_io_ops_dkey_punch_latency_stddev"
        "engine_io_ops_ec_agg_active"
        "engine_io_ops_ec_agg_active_max"
        "engine_io_ops_ec_agg_active_mean"
        "engine_io_ops_ec_agg_active_min"
        "engine_io_ops_ec_agg_active_stddev"
        "engine_io_ops_ec_agg_latency"
        "engine_io_ops_ec_agg_latency_max"
        "engine_io_ops_ec_agg_latency_mean"
        "engine_io_ops_ec_agg_latency_min"
        "engine_io_ops_ec_agg_latency_stddev"
        "engine_io_ops_ec_rep_active"
        "engine_io_ops_ec_rep_active_max"
        "engine_io_ops_ec_rep_active_mean"
        "engine_io_ops_ec_rep_active_min"
        "engine_io_ops_ec_rep_active_stddev"
        "engine_io_ops_ec_rep_latency"
        "engine_io_ops_ec_rep_latency_max"
        "engine_io_ops_ec_rep_latency_mean"
        "engine_io_ops_ec_rep_latency_min"
        "engine_io_ops_ec_rep_latency_stddev"
        "engine_io_ops_fetch_active"
        "engine_io_ops_fetch_active_max"
        "engine_io_ops_fetch_active_mean"
        "engine_io_ops_fetch_active_min"
        "engine_io_ops_fetch_active_stddev"
        "engine_io_ops_key_query_active"
        "engine_io_ops_key_query_active_max"
        "engine_io_ops_key_query_active_mean"
        "engine_io_ops_key_query_active_min"
        "engine_io_ops_key_query_active_stddev"
        "engine_io_ops_key_query_latency"
        "engine_io_ops_key_query_latency_max"
        "engine_io_ops_key_query_latency_mean"
        "engine_io_ops_key_query_latency_min"
        "engine_io_ops_key_query_latency_stddev"
        "engine_io_ops_migrate_active"
        "engine_io_ops_migrate_active_max"
        "engine_io_ops_migrate_active_mean"
        "engine_io_ops_migrate_active_min"
        "engine_io_ops_migrate_active_stddev"
        "engine_io_ops_migrate_latency"
        "engine_io_ops_migrate_latency_max"
        "engine_io_ops_migrate_latency_mean"
        "engine_io_ops_migrate_latency_min"
        "engine_io_ops_migrate_latency_stddev"
        "engine_io_ops_obj_enum_active"
        "engine_io_ops_obj_enum_active_max"
        "engine_io_ops_obj_enum_active_mean"
        "engine_io_ops_obj_enum_active_min"
        "engine_io_ops_obj_enum_active_stddev"
        "engine_io_ops_obj_enum_latency"
        "engine_io_ops_obj_enum_latency_max"
        "engine_io_ops_obj_enum_latency_mean"
        "engine_io_ops_obj_enum_latency_min"
        "engine_io_ops_obj_enum_latency_stddev"
        "engine_io_ops_obj_punch_active"
        "engine_io_ops_obj_punch_active_max"
        "engine_io_ops_obj_punch_active_mean"
        "engine_io_ops_obj_punch_active_min"
        "engine_io_ops_obj_punch_active_stddev"
        "engine_io_ops_obj_punch_latency"
        "engine_io_ops_obj_punch_latency_max"
        "engine_io_ops_obj_punch_latency_mean"
        "engine_io_ops_obj_punch_latency_min"
        "engine_io_ops_obj_punch_latency_stddev"
        "engine_io_ops_obj_sync_active"
        "engine_io_ops_obj_sync_active_max"
        "engine_io_ops_obj_sync_active_mean"
        "engine_io_ops_obj_sync_active_min"
        "engine_io_ops_obj_sync_active_stddev"
        "engine_io_ops_obj_sync_latency"
        "engine_io_ops_obj_sync_latency_max"
        "engine_io_ops_obj_sync_latency_mean"
        "engine_io_ops_obj_sync_latency_min"
        "engine_io_ops_obj_sync_latency_stddev"
        "engine_io_ops_recx_enum_active"
        "engine_io_ops_recx_enum_active_max"
        "engine_io_ops_recx_enum_active_mean"
        "engine_io_ops_recx_enum_active_min"
        "engine_io_ops_recx_enum_active_stddev"
        "engine_io_ops_recx_enum_latency"
        "engine_io_ops_recx_enum_latency_max"
        "engine_io_ops_recx_enum_latency_mean"
        "engine_io_ops_recx_enum_latency_min"
        "engine_io_ops_recx_enum_latency_stddev"
        "engine_io_ops_tgt_akey_punch_active"
        "engine_io_ops_tgt_akey_punch_active_max"
        "engine_io_ops_tgt_akey_punch_active_mean"
        "engine_io_ops_tgt_akey_punch_active_min"
        "engine_io_ops_tgt_akey_punch_active_stddev"
        "engine_io_ops_tgt_akey_punch_latency"
        "engine_io_ops_tgt_akey_punch_latency_max"
        "engine_io_ops_tgt_akey_punch_latency_mean"
        "engine_io_ops_tgt_akey_punch_latency_min"
        "engine_io_ops_tgt_akey_punch_latency_stddev"
        "engine_io_ops_tgt_dkey_punch_active"
        "engine_io_ops_tgt_dkey_punch_active_max"
        "engine_io_ops_tgt_dkey_punch_active_mean"
        "engine_io_ops_tgt_dkey_punch_active_min"
        "engine_io_ops_tgt_dkey_punch_active_stddev"
        "engine_io_ops_tgt_dkey_punch_latency"
        "engine_io_ops_tgt_dkey_punch_latency_max"
        "engine_io_ops_tgt_dkey_punch_latency_mean"
        "engine_io_ops_tgt_dkey_punch_latency_min"
        "engine_io_ops_tgt_dkey_punch_latency_stddev"
        "engine_io_ops_tgt_punch_active"
        "engine_io_ops_tgt_punch_active_max"
        "engine_io_ops_tgt_punch_active_mean"
        "engine_io_ops_tgt_punch_active_min"
        "engine_io_ops_tgt_punch_active_stddev"
        "engine_io_ops_tgt_punch_latency"
        "engine_io_ops_tgt_punch_latency_max"
        "engine_io_ops_tgt_punch_latency_mean"
        "engine_io_ops_tgt_punch_latency_min"
        "engine_io_ops_tgt_punch_latency_stddev"
        "engine_io_ops_tgt_update_active"
        "engine_io_ops_tgt_update_active_max"
        "engine_io_ops_tgt_update_active_mean"
        "engine_io_ops_tgt_update_active_min"
        "engine_io_ops_tgt_update_active_stddev"
        "engine_io_ops_update_active"
        "engine_io_ops_update_active_max"
        "engine_io_ops_update_active_mean"
        "engine_io_ops_update_active_min"
        "engine_io_ops_update_active_stddev"
    ]
    ENGINE_NET_METRICS = [
        "engine_net_ofi_sockets_failed_addr",
        "engine_net_ofi_sockets_req_timeout",
        "engine_net_uri_lookup_other",
        "engine_net_uri_lookup_self",
        "engine_net_ofi_sockets_uri_lookup_timeout"]
    ENGINE_RANK_METRICS = [
        "engine_rank"]
    GO_METRICS = [
        "go_gc_duration_seconds",
        "go_goroutines",
        "go_info",
        "go_memstats_alloc_bytes",
        "go_memstats_alloc_bytes_total",
        "go_memstats_buck_hash_sys_bytes",
        "go_memstats_frees_total",
        "go_memstats_gc_cpu_fraction",
        "go_memstats_gc_sys_bytes",
        "go_memstats_heap_alloc_bytes",
        "go_memstats_heap_idle_bytes",
        "go_memstats_heap_inuse_bytes",
        "go_memstats_heap_objects",
        "go_memstats_heap_released_bytes",
        "go_memstats_heap_sys_bytes",
        "go_memstats_last_gc_time_seconds",
        "go_memstats_lookups_total",
        "go_memstats_mallocs_total",
        "go_memstats_mcache_inuse_bytes",
        "go_memstats_mcache_sys_bytes",
        "go_memstats_mspan_inuse_bytes",
        "go_memstats_mspan_sys_bytes",
        "go_memstats_next_gc_bytes",
        "go_memstats_other_sys_bytes",
        "go_memstats_stack_inuse_bytes",
        "go_memstats_stack_sys_bytes",
        "go_memstats_sys_bytes",
        "go_threads"]
    PROCESS_METRICS = [
        "process_cpu_seconds_total",
        "process_max_fds",
        "process_open_fds",
        "process_resident_memory_bytes",
        "process_start_time_seconds",
        "process_virtual_memory_bytes",
        "process_virtual_memory_max_bytes"]
    ENGINE_NVME_METRICS = [
        "engine_nvme_<id>_commands_checksum_mismatch",
        "engine_nvme_<id>_commands_ctrl_busy_time",
        "engine_nvme_<id>_commands_data_units_read",
        "engine_nvme_<id>_commands_data_units_written",
        "engine_nvme_<id>_commands_host_read_cmds",
        "engine_nvme_<id>_commands_host_write_cmds",
        "engine_nvme_<id>_commands_media_errs",
        "engine_nvme_<id>_commands_read_errs",
        "engine_nvme_<id>_commands_unmap_errs",
        "engine_nvme_<id>_commands_write_errs",
        "engine_nvme_<id>_power_cycles",
        "engine_nvme_<id>_power_on_hours",
        "engine_nvme_<id>_read_only_warn",
        "engine_nvme_<id>_reliability_avail_spare",
        "engine_nvme_<id>_reliability_avail_spare_threshold",
        "engine_nvme_<id>_reliability_avail_spare_warn",
        "engine_nvme_<id>_reliability_percentage_used",
        "engine_nvme_<id>_reliability_reliability_warn",
        "engine_nvme_<id>_temp_crit_time",
        "engine_nvme_<id>_temp_current",
        "engine_nvme_<id>_temp_warn",
        "engine_nvme_<id>_temp_warn_time",
        "engine_nvme_<id>_unsafe_shutdowns",
        "engine_nvme_<id>_volatile_mem_warn"]

    def __init__(self, dmg, servers):
        """Create a TelemetryUtils object.

        Args:
            dmg (DmgCommand): the DmgCommand object configured to communicate
                with the servers
            servers (list): a list of server host names
        """
        self.log = getLogger(__name__)
        self.dmg = dmg
        self.hosts = NodeSet.fromlist(servers)

    def get_all_server_metrics_names(self, server):
        """Get all the telemetry metrics names for this server.

        Args:
            server (DaosServerCommand): the server from which to determine what
                metrics will be available

        Returns:
            list: all of the telemetry metrics names for this server

        """
        all_metrics_names = list(self.ENGINE_EVENT_METRICS)
        all_metrics_names.extend(self.ENGINE_IO_METRICS)
        all_metrics_names.extend(self.ENGINE_NET_METRICS)
        all_metrics_names.extend(self.ENGINE_RANK_METRICS)
        all_metrics_names.extend(self.GO_METRICS)
        all_metrics_names.extend(self.PROCESS_METRICS)

        # Add NVMe metrics for any NVMe devices configured for this server
        for nvme_list in server.manager.job.get_engine_values("bdev_list"):
            for nvme in nvme_list if nvme_list is not None else []:
                # Replace the '<id>' placeholder with the actual NVMe ID
                nvme_id = nvme.replace(":", "_").replace(".", "_")
                nvme_metrics = [
                    name.replace("<id>", nvme_id)
                    for name in self.ENGINE_NVME_METRICS]
                all_metrics_names.extend(nvme_metrics)

        return all_metrics_names

    def list_metrics(self):
        """List the available metrics for each host.

        Returns:
            dict: a dictionary of host keys linked to a list of metric names

        """
        info = {}
        self.log.info("Listing telemetry metrics from %s", self.hosts)
        for host in self.hosts:
            data = self.dmg.telemetry_metrics_list(host=host)
            info[host] = []
            if "response" in data:
                if "available_metric_sets" in data["response"]:
                    for entry in data["response"]["available_metric_sets"]:
                        if "name" in entry:
                            info[host].append(entry["name"])
        return info

    def get_metrics(self, name):
        """Obtain the specified metric information for each host.

        Args:
            name (str): Comma-separated list of metric names to query.

        Returns:
            dict: a dictionary of host keys linked to metric data for each
                metric name specified

        """
        info = {}
        self.log.info("Querying telemetry metric %s from %s", name, self.hosts)
        for host in self.hosts:
            data = self.dmg.telemetry_metrics_query(host=host, metrics=name)
            info[host] = {}
            if "response" in data:
                if "metric_sets" in data["response"]:
                    for entry in data["response"]["metric_sets"]:
                        info[host][entry["name"]] = {
                            "description": entry["description"],
                            "metrics": entry["metrics"]
                        }
        return info
