#!/usr/bin/python
"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
import re
from ClusterShell.NodeSet import NodeSet


class TelemetryUtils():
    # pylint: disable=too-many-nested-blocks
    """Defines a object used to verify telemetry information."""

    ENGINE_CONTAINER_METRICS = [
        "engine_pool_ops_cont_open",
        "engine_pool_ops_cont_close",
        "engine_pool_ops_cont_create",
        "engine_pool_ops_cont_destroy",
        "engine_pool_ops_cont_query"]
    ENGINE_POOL_METRICS = [
        "engine_pool_entries_dtx_batched_degree",
        "engine_pool_entries_dtx_batched_total",
        "engine_pool_ops_akey_enum",
        "engine_pool_ops_akey_punch",
        "engine_pool_ops_compound",
        "engine_pool_ops_dkey_enum",
        "engine_pool_ops_dkey_punch",
        "engine_pool_ops_dtx_abort",
        "engine_pool_ops_dtx_check",
        "engine_pool_ops_dtx_commit",
        "engine_pool_ops_dtx_refresh",
        "engine_pool_ops_ec_agg",
        "engine_pool_ops_ec_rep",
        "engine_pool_ops_fetch",
        "engine_pool_ops_key_query",
        "engine_pool_ops_migrate",
        "engine_pool_ops_obj_enum",
        "engine_pool_ops_obj_punch",
        "engine_pool_ops_obj_sync",
        "engine_pool_ops_recx_enum",
        "engine_pool_ops_tgt_akey_punch",
        "engine_pool_ops_tgt_dkey_punch",
        "engine_pool_ops_tgt_punch",
        "engine_pool_ops_tgt_update",
        "engine_pool_ops_update",
        "engine_pool_ops_pool_evict",
        "engine_pool_ops_pool_connect",
        "engine_pool_ops_pool_disconnect",
        "engine_pool_ops_pool_query",
        "engine_pool_ops_pool_query_space",
        "engine_pool_resent",
        "engine_pool_uncommitted_retry",
        "engine_pool_restarted",
        "engine_pool_started_at",
        "engine_pool_xferred_fetch",
        "engine_pool_xferred_update"]
    ENGINE_EVENT_METRICS = [
        "engine_events_dead_ranks",
        "engine_events_last_event_ts",
        "engine_servicing_at",
        "engine_started_at"]
    ENGINE_SCHED_METRICS = [
        "engine_sched_total_time",
        "engine_sched_relax_time",
        "engine_sched_wait_queue",
        "engine_sched_sleep_queue",
        "engine_sched_cycle_duration",
        "engine_sched_cycle_duration_max",
        "engine_sched_cycle_duration_mean",
        "engine_sched_cycle_duration_min",
        "engine_sched_cycle_duration_stddev",
        "engine_sched_cycle_size",
        "engine_sched_cycle_size_max",
        "engine_sched_cycle_size_mean",
        "engine_sched_cycle_size_min",
        "engine_sched_cycle_size_stddev"]
    ENGINE_IO_DTX_COMMITTABLE_METRICS = [
        "engine_io_dtx_committable",
        "engine_io_dtx_committable_max",
        "engine_io_dtx_committable_mean",
        "engine_io_dtx_committable_min",
        "engine_io_dtx_committable_stddev"]
    ENGINE_IO_DTX_COMMITTED_METRICS = [
        "engine_io_dtx_committed",
        "engine_io_dtx_committed_max",
        "engine_io_dtx_committed_mean",
        "engine_io_dtx_committed_min",
        "engine_io_dtx_committed_stddev"]
    ENGINE_IO_LATENCY_FETCH_METRICS = [
        "engine_io_latency_fetch",
        "engine_io_latency_fetch_max",
        "engine_io_latency_fetch_mean",
        "engine_io_latency_fetch_min",
        "engine_io_latency_fetch_stddev"]
    ENGINE_IO_LATENCY_UPDATE_METRICS = [
        "engine_io_latency_update",
        "engine_io_latency_update_max",
        "engine_io_latency_update_mean",
        "engine_io_latency_update_min",
        "engine_io_latency_update_stddev"]
    ENGINE_IO_OPS_AKEY_ENUM_METRICS = [
        "engine_io_ops_akey_enum_active",
        "engine_io_ops_akey_enum_active_max",
        "engine_io_ops_akey_enum_active_mean",
        "engine_io_ops_akey_enum_active_min",
        "engine_io_ops_akey_enum_active_stddev"]
    ENGINE_IO_OPS_AKEY_ENUM_LATENCY_METRICS = [
        "engine_io_ops_akey_enum_latency",
        "engine_io_ops_akey_enum_latency_max",
        "engine_io_ops_akey_enum_latency_mean",
        "engine_io_ops_akey_enum_latency_min",
        "engine_io_ops_akey_enum_latency_stddev"]
    ENGINE_IO_OPS_AKEY_PUNCH_ACTIVE_METRICS = [
        "engine_io_ops_akey_punch_active",
        "engine_io_ops_akey_punch_active_max",
        "engine_io_ops_akey_punch_active_mean",
        "engine_io_ops_akey_punch_active_min",
        "engine_io_ops_akey_punch_active_stddev"]
    ENGINE_IO_OPS_AKEY_PUNCH_LATENCY_METRICS = [
        "engine_io_ops_akey_punch_latency",
        "engine_io_ops_akey_punch_latency_max",
        "engine_io_ops_akey_punch_latency_mean",
        "engine_io_ops_akey_punch_latency_min",
        "engine_io_ops_akey_punch_latency_stddev"]
    ENGINE_IO_OPS_COMPOUND_ACTIVE_METRICS = [
        "engine_io_ops_compound_active",
        "engine_io_ops_compound_active_max",
        "engine_io_ops_compound_active_mean",
        "engine_io_ops_compound_active_min",
        "engine_io_ops_compound_active_stddev"]
    ENGINE_IO_OPS_COMPOUND_LATENCY_METRICS = [
        "engine_io_ops_compound_latency",
        "engine_io_ops_compound_latency_max",
        "engine_io_ops_compound_latency_mean",
        "engine_io_ops_compound_latency_min",
        "engine_io_ops_compound_latency_stddev"]
    ENGINE_IO_OPS_DKEY_ENUM_ACTIVE_METRICS = [
        "engine_io_ops_dkey_enum_active",
        "engine_io_ops_dkey_enum_active_max",
        "engine_io_ops_dkey_enum_active_mean",
        "engine_io_ops_dkey_enum_active_min",
        "engine_io_ops_dkey_enum_active_stddev"]
    ENGINE_IO_OPS_DKEY_ENUM_LATENCY_METRICS = [
        "engine_io_ops_dkey_enum_latency",
        "engine_io_ops_dkey_enum_latency_max",
        "engine_io_ops_dkey_enum_latency_mean",
        "engine_io_ops_dkey_enum_latency_min",
        "engine_io_ops_dkey_enum_latency_stddev"]
    ENGINE_IO_OPS_DKEY_PUNCH_ACTIVE_METRICS = [
        "engine_io_ops_dkey_punch_active",
        "engine_io_ops_dkey_punch_active_max",
        "engine_io_ops_dkey_punch_active_mean",
        "engine_io_ops_dkey_punch_active_min",
        "engine_io_ops_dkey_punch_active_stddev"]
    ENGINE_IO_OPS_DKEY_PUNCH_LATENCY_METRICS = [
        "engine_io_ops_dkey_punch_latency",
        "engine_io_ops_dkey_punch_latency_max",
        "engine_io_ops_dkey_punch_latency_mean",
        "engine_io_ops_dkey_punch_latency_min",
        "engine_io_ops_dkey_punch_latency_stddev"]
    ENGINE_IO_OPS_EC_AGG_ACTIVE_METRICS = [
        "engine_io_ops_ec_agg_active",
        "engine_io_ops_ec_agg_active_max",
        "engine_io_ops_ec_agg_active_mean",
        "engine_io_ops_ec_agg_active_min",
        "engine_io_ops_ec_agg_active_stddev"]
    ENGINE_IO_OPS_EC_AGG_LATENCY_METRICS = [
        "engine_io_ops_ec_agg_latency",
        "engine_io_ops_ec_agg_latency_max",
        "engine_io_ops_ec_agg_latency_mean",
        "engine_io_ops_ec_agg_latency_min",
        "engine_io_ops_ec_agg_latency_stddev"]
    ENGINE_IO_OPS_EC_REP_ACTIVE_METRICS = [
        "engine_io_ops_ec_rep_active",
        "engine_io_ops_ec_rep_active_max",
        "engine_io_ops_ec_rep_active_mean",
        "engine_io_ops_ec_rep_active_min",
        "engine_io_ops_ec_rep_active_stddev"]
    ENGINE_IO_OPS_EC_REP_LATENCY_METRICS = [
        "engine_io_ops_ec_rep_latency",
        "engine_io_ops_ec_rep_latency_max",
        "engine_io_ops_ec_rep_latency_mean",
        "engine_io_ops_ec_rep_latency_min",
        "engine_io_ops_ec_rep_latency_stddev"]
    ENGINE_IO_OPS_FETCH_ACTIVE_METRICS = [
        "engine_io_ops_fetch_active",
        "engine_io_ops_fetch_active_max",
        "engine_io_ops_fetch_active_mean",
        "engine_io_ops_fetch_active_min",
        "engine_io_ops_fetch_active_stddev"]
    ENGINE_IO_OPS_KEY_QUERY_ACTIVE_METRICS = [
        "engine_io_ops_key_query_active",
        "engine_io_ops_key_query_active_max",
        "engine_io_ops_key_query_active_mean",
        "engine_io_ops_key_query_active_min",
        "engine_io_ops_key_query_active_stddev"]
    ENGINE_IO_OPS_KEY_QUERY_LATENCY_METRICS = [
        "engine_io_ops_key_query_latency",
        "engine_io_ops_key_query_latency_max",
        "engine_io_ops_key_query_latency_mean",
        "engine_io_ops_key_query_latency_min",
        "engine_io_ops_key_query_latency_stddev"]
    ENGINE_IO_OPS_MIGRATE_ACTIVE_METRICS = [
        "engine_io_ops_migrate_active",
        "engine_io_ops_migrate_active_max",
        "engine_io_ops_migrate_active_mean",
        "engine_io_ops_migrate_active_min",
        "engine_io_ops_migrate_active_stddev"]
    ENGINE_IO_OPS_MIGRATE_LATENCY_METRICS = [
        "engine_io_ops_migrate_latency",
        "engine_io_ops_migrate_latency_max",
        "engine_io_ops_migrate_latency_mean",
        "engine_io_ops_migrate_latency_min",
        "engine_io_ops_migrate_latency_stddev"]
    ENGINE_IO_OPS_OBJ_ENUM_ACTIVE_METRICS = [
        "engine_io_ops_obj_enum_active",
        "engine_io_ops_obj_enum_active_max",
        "engine_io_ops_obj_enum_active_mean",
        "engine_io_ops_obj_enum_active_min",
        "engine_io_ops_obj_enum_active_stddev"]
    ENGINE_IO_OPS_OBJ_ENUM_LATENCY_METRICS = [
        "engine_io_ops_obj_enum_latency",
        "engine_io_ops_obj_enum_latency_max",
        "engine_io_ops_obj_enum_latency_mean",
        "engine_io_ops_obj_enum_latency_min",
        "engine_io_ops_obj_enum_latency_stddev"]
    ENGINE_IO_OPS_OBJ_PUNCH_ACTIVE_METRICS = [
        "engine_io_ops_obj_punch_active",
        "engine_io_ops_obj_punch_active_max",
        "engine_io_ops_obj_punch_active_mean",
        "engine_io_ops_obj_punch_active_min",
        "engine_io_ops_obj_punch_active_stddev"]
    ENGINE_IO_OPS_OBJ_PUNCH_LATENCY_METRICS = [
        "engine_io_ops_obj_punch_latency",
        "engine_io_ops_obj_punch_latency_max",
        "engine_io_ops_obj_punch_latency_mean",
        "engine_io_ops_obj_punch_latency_min",
        "engine_io_ops_obj_punch_latency_stddev"]
    ENGINE_IO_OPS_OBJ_SYNC_ACTIVE_METRICS = [
        "engine_io_ops_obj_sync_active",
        "engine_io_ops_obj_sync_active_max",
        "engine_io_ops_obj_sync_active_mean",
        "engine_io_ops_obj_sync_active_min",
        "engine_io_ops_obj_sync_active_stddev"]
    ENGINE_IO_OPS_OBJ_SYNC_LATENCY_METRICS = [
        "engine_io_ops_obj_sync_latency",
        "engine_io_ops_obj_sync_latency_max",
        "engine_io_ops_obj_sync_latency_mean",
        "engine_io_ops_obj_sync_latency_min",
        "engine_io_ops_obj_sync_latency_stddev"]
    ENGINE_IO_OPS_RECX_ENUM_ACTIVE_METRICS = [
        "engine_io_ops_recx_enum_active",
        "engine_io_ops_recx_enum_active_max",
        "engine_io_ops_recx_enum_active_mean",
        "engine_io_ops_recx_enum_active_min",
        "engine_io_ops_recx_enum_active_stddev"]
    ENGINE_IO_OPS_RECX_ENUM_LATENCY_METRICS = [
        "engine_io_ops_recx_enum_latency",
        "engine_io_ops_recx_enum_latency_max",
        "engine_io_ops_recx_enum_latency_mean",
        "engine_io_ops_recx_enum_latency_min",
        "engine_io_ops_recx_enum_latency_stddev"]
    ENGINE_IO_OPS_TGT_AKEY_PUNCH_ACTIVE_METRICS = [
        "engine_io_ops_tgt_akey_punch_active",
        "engine_io_ops_tgt_akey_punch_active_max",
        "engine_io_ops_tgt_akey_punch_active_mean",
        "engine_io_ops_tgt_akey_punch_active_min",
        "engine_io_ops_tgt_akey_punch_active_stddev"]
    ENGINE_IO_OPS_TGT_AKEY_PUNCH_LATENCY_METRICS = [
        "engine_io_ops_tgt_akey_punch_latency",
        "engine_io_ops_tgt_akey_punch_latency_max",
        "engine_io_ops_tgt_akey_punch_latency_mean",
        "engine_io_ops_tgt_akey_punch_latency_min",
        "engine_io_ops_tgt_akey_punch_latency_stddev"]
    ENGINE_IO_OPS_TGT_DKEY_PUNCH_ACTIVE_METRICS = [
        "engine_io_ops_tgt_dkey_punch_active",
        "engine_io_ops_tgt_dkey_punch_active_max",
        "engine_io_ops_tgt_dkey_punch_active_mean",
        "engine_io_ops_tgt_dkey_punch_active_min",
        "engine_io_ops_tgt_dkey_punch_active_stddev"]
    ENGINE_IO_OPS_TGT_DKEY_PUNCH_LATENCY_METRICS = [
        "engine_io_ops_tgt_dkey_punch_latency",
        "engine_io_ops_tgt_dkey_punch_latency_max",
        "engine_io_ops_tgt_dkey_punch_latency_mean",
        "engine_io_ops_tgt_dkey_punch_latency_min",
        "engine_io_ops_tgt_dkey_punch_latency_stddev"]
    ENGINE_IO_OPS_TGT_PUNCH_ACTIVE_METRICS = [
        "engine_io_ops_tgt_punch_active",
        "engine_io_ops_tgt_punch_active_max",
        "engine_io_ops_tgt_punch_active_mean",
        "engine_io_ops_tgt_punch_active_min",
        "engine_io_ops_tgt_punch_active_stddev"]
    ENGINE_IO_OPS_TGT_PUNCH_LATENCY_METRICS = [
        "engine_io_ops_tgt_punch_latency",
        "engine_io_ops_tgt_punch_latency_max",
        "engine_io_ops_tgt_punch_latency_mean",
        "engine_io_ops_tgt_punch_latency_min",
        "engine_io_ops_tgt_punch_latency_stddev"]
    ENGINE_IO_OPS_TGT_UPDATE_ACTIVE_METRICS = [
        "engine_io_ops_tgt_update_active",
        "engine_io_ops_tgt_update_active_max",
        "engine_io_ops_tgt_update_active_mean",
        "engine_io_ops_tgt_update_active_min",
        "engine_io_ops_tgt_update_active_stddev"]
    ENGINE_IO_OPS_UPDATE_ACTIVE_METRICS = [
        "engine_io_ops_update_active",
        "engine_io_ops_update_active_max",
        "engine_io_ops_update_active_mean",
        "engine_io_ops_update_active_min",
        "engine_io_ops_update_active_stddev"]
    ENGINE_IO_METRICS = ENGINE_IO_DTX_COMMITTABLE_METRICS +\
        ENGINE_IO_DTX_COMMITTED_METRICS +\
        ENGINE_IO_LATENCY_FETCH_METRICS +\
        ENGINE_IO_LATENCY_UPDATE_METRICS +\
        ENGINE_IO_OPS_AKEY_ENUM_METRICS +\
        ENGINE_IO_OPS_AKEY_ENUM_LATENCY_METRICS +\
        ENGINE_IO_OPS_AKEY_PUNCH_ACTIVE_METRICS +\
        ENGINE_IO_OPS_AKEY_PUNCH_LATENCY_METRICS +\
        ENGINE_IO_OPS_COMPOUND_ACTIVE_METRICS +\
        ENGINE_IO_OPS_COMPOUND_LATENCY_METRICS +\
        ENGINE_IO_OPS_DKEY_ENUM_ACTIVE_METRICS +\
        ENGINE_IO_OPS_DKEY_ENUM_LATENCY_METRICS +\
        ENGINE_IO_OPS_DKEY_PUNCH_ACTIVE_METRICS +\
        ENGINE_IO_OPS_DKEY_PUNCH_LATENCY_METRICS +\
        ENGINE_IO_OPS_EC_AGG_ACTIVE_METRICS +\
        ENGINE_IO_OPS_EC_AGG_LATENCY_METRICS +\
        ENGINE_IO_OPS_EC_REP_ACTIVE_METRICS +\
        ENGINE_IO_OPS_EC_REP_LATENCY_METRICS +\
        ENGINE_IO_OPS_FETCH_ACTIVE_METRICS +\
        ENGINE_IO_OPS_KEY_QUERY_ACTIVE_METRICS +\
        ENGINE_IO_OPS_KEY_QUERY_LATENCY_METRICS +\
        ENGINE_IO_OPS_MIGRATE_ACTIVE_METRICS +\
        ENGINE_IO_OPS_MIGRATE_LATENCY_METRICS +\
        ENGINE_IO_OPS_OBJ_ENUM_ACTIVE_METRICS +\
        ENGINE_IO_OPS_OBJ_ENUM_LATENCY_METRICS +\
        ENGINE_IO_OPS_OBJ_PUNCH_ACTIVE_METRICS +\
        ENGINE_IO_OPS_OBJ_PUNCH_LATENCY_METRICS +\
        ENGINE_IO_OPS_OBJ_SYNC_ACTIVE_METRICS +\
        ENGINE_IO_OPS_OBJ_SYNC_LATENCY_METRICS +\
        ENGINE_IO_OPS_RECX_ENUM_ACTIVE_METRICS +\
        ENGINE_IO_OPS_RECX_ENUM_LATENCY_METRICS +\
        ENGINE_IO_OPS_TGT_AKEY_PUNCH_ACTIVE_METRICS +\
        ENGINE_IO_OPS_TGT_AKEY_PUNCH_LATENCY_METRICS +\
        ENGINE_IO_OPS_TGT_DKEY_PUNCH_ACTIVE_METRICS +\
        ENGINE_IO_OPS_TGT_DKEY_PUNCH_LATENCY_METRICS +\
        ENGINE_IO_OPS_TGT_PUNCH_ACTIVE_METRICS +\
        ENGINE_IO_OPS_TGT_PUNCH_LATENCY_METRICS +\
        ENGINE_IO_OPS_TGT_UPDATE_ACTIVE_METRICS +\
        ENGINE_IO_OPS_UPDATE_ACTIVE_METRICS
    ENGINE_NET_METRICS = [
        "engine_net_<provider>_failed_addr",
        "engine_net_<provider>_req_timeout",
        "engine_net_<provider>_uri_lookup_timeout",
        "engine_net_uri_lookup_other",
        "engine_net_uri_lookup_self"]
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
    ENGINE_NVME_HEALTH_METRICS = [
        "engine_nvme_<id>_commands_data_units_written",
        "engine_nvme_<id>_commands_data_units_read",
        "engine_nvme_<id>_commands_host_write_cmds",
        "engine_nvme_<id>_commands_host_read_cmds",
        "engine_nvme_<id>_commands_media_errs",
        "engine_nvme_<id>_commands_read_errs",
        "engine_nvme_<id>_commands_write_errs",
        "engine_nvme_<id>_commands_unmap_errs",
        "engine_nvme_<id>_commands_checksum_mismatch",
        "engine_nvme_<id>_power_cycles",
        "engine_nvme_<id>_commands_ctrl_busy_time",
        "engine_nvme_<id>_power_on_hours",
        "engine_nvme_<id>_unsafe_shutdowns"]
    ENGINE_NVME_TEMP_METRICS = [
        "engine_nvme_<id>_temp_current"]
    ENGINE_NVME_TEMP_TIME_METRICS = [
        "engine_nvme_<id>_temp_warn_time",
        "engine_nvme_<id>_temp_crit_time"]
    ENGINE_NVME_RELIABILITY_METRICS = [
        "engine_nvme_<id>_reliability_avail_spare",
        "engine_nvme_<id>_reliability_avail_spare_threshold"]
    ENGINE_NVME_CRIT_WARN_METRICS = [
        "engine_nvme_<id>_reliability_avail_spare_warn",
        "engine_nvme_<id>_reliability_reliability_warn",
        "engine_nvme_<id>_temp_warn",
        "engine_nvme_<id>_read_only_warn",
        "engine_nvme_<id>_volatile_mem_warn"]
    ENGINE_NVME_INTEL_VENDOR_METRICS = [
        "engine_nvme_<id>_vendor_program_fail_cnt_norm",
        "engine_nvme_<id>_vendor_program_fail_cnt_raw",
        "engine_nvme_<id>_vendor_erase_fail_cnt_norm",
        "engine_nvme_<id>_vendor_erase_fail_cnt_raw",
        "engine_nvme_<id>_vendor_wear_leveling_cnt_norm",
        "engine_nvme_<id>_vendor_wear_leveling_cnt_min",
        "engine_nvme_<id>_vendor_wear_leveling_cnt_max",
        "engine_nvme_<id>_vendor_wear_leveling_cnt_avg",
        "engine_nvme_<id>_vendor_endtoend_err_cnt_raw",
        "engine_nvme_<id>_vendor_crc_err_cnt_raw",
        "engine_nvme_<id>_vendor_media_wear_raw",
        "engine_nvme_<id>_vendor_host_reads_raw",
        "engine_nvme_<id>_vendor_crc_workload_timer_raw",
        "engine_nvme_<id>_vendor_thermal_throttle_status_raw",
        "engine_nvme_<id>_vendor_thermal_throttle_event_cnt",
        "engine_nvme_<id>_vendor_retry_buffer_overflow_cnt",
        "engine_nvme_<id>_vendor_pll_lock_loss_cnt",
        "engine_nvme_<id>_vendor_nand_bytes_written",
        "engine_nvme_<id>_vendor_host_bytes_written"]
    ENGINE_NVME_METRICS = ENGINE_NVME_HEALTH_METRICS +\
        ENGINE_NVME_TEMP_METRICS +\
        ENGINE_NVME_TEMP_TIME_METRICS +\
        ENGINE_NVME_RELIABILITY_METRICS +\
        ENGINE_NVME_CRIT_WARN_METRICS +\
        ENGINE_NVME_INTEL_VENDOR_METRICS

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

    def get_all_server_metrics_names(self, server, with_pools=False):
        """Get all the telemetry metrics names for this server.

        Args:
            server (DaosServerCommand): the server from which to determine what
                metrics will be available

        Returns:
            list: all of the telemetry metrics names for this server

        """
        all_metrics_names = list(self.ENGINE_EVENT_METRICS)
        all_metrics_names.extend(self.ENGINE_SCHED_METRICS)
        all_metrics_names.extend(self.ENGINE_IO_METRICS)
        all_metrics_names.extend(self.ENGINE_RANK_METRICS)
        all_metrics_names.extend(self.GO_METRICS)
        all_metrics_names.extend(self.PROCESS_METRICS)
        if with_pools:
            all_metrics_names.extend(self.ENGINE_POOL_METRICS)
            all_metrics_names.extend(self.ENGINE_CONTAINER_METRICS)

        # Add engine network metrics for the configured provider
        try:
            provider = re.sub("[+;]", "_", server.manager.job.get_config_value("provider"))
            if provider == "ofi_tcp":
                provider = "ofi_tcp_ofi_rxm"
            elif provider == "ofi_verbs":
                provider = "ofi_verbs_ofi_rxm"
        except TypeError:
            provider = "ofi_tcp_ofi_rxm"
        net_metrics = [name.replace("<provider>", provider) for name in self.ENGINE_NET_METRICS]
        all_metrics_names.extend(net_metrics)

        # Add NVMe metrics for any NVMe devices configured for this server
        for nvme_list in server.manager.job.get_engine_values("bdev_list"):
            for nvme in nvme_list if nvme_list is not None else []:
                # Replace the '<id>' placeholder with the actual NVMe ID
                nvme_id = nvme.replace(":", "_").replace(".", "_")
                nvme_metrics = [name.replace("<id>", nvme_id) for name in self.ENGINE_NVME_METRICS]
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

    def get_container_metrics(self):
        """Get the container telemetry metrics.

        Returns:
            dict: dictionary of dictionaries of container metric names and
                values per server host key

        """
        data = {}
        info = self.get_metrics(",".join(self.ENGINE_CONTAINER_METRICS))
        self.log.info("Container Telemetry Information")
        for host in info:
            data[host] = {name: 0 for name in self.ENGINE_CONTAINER_METRICS}
            for name in self.ENGINE_CONTAINER_METRICS:
                if name in info[host]:
                    for metric in info[host][name]["metrics"]:
                        self.log.info(
                            "  %s (%s): %s (%s)",
                            info[host][name]["description"], name,
                            metric["value"], host)
                        data[host][name] = metric["value"]
        return data

    def get_pool_metrics(self, specific_metrics=None):
        """Get the pool telemetry metrics.

        Args:
            specific_metrics(list): list of specific pool metrics
        Returns:
            dict: dictionary of dictionaries of pool metric names and
                values per server host key

        """
        data = {}
        if specific_metrics is None:
            specific_metrics = self.ENGINE_POOL_METRICS
        info = self.get_metrics(",".join(specific_metrics))
        self.log.info("Pool Telemetry Information")
        for name in specific_metrics:
            for index, host in enumerate(info):
                if name in info[host]:
                    if index == 0:
                        self.log.info(
                            "  %s (%s):",
                            name, info[host][name]["description"])
                        self.log.info(
                            "    %-12s %-4s %-6s %s",
                            "Host", "Rank", "Target", "Value")
                    if name not in data:
                        data[name] = {}
                    if host not in data[name]:
                        data[name][host] = {}
                    for metric in info[host][name]["metrics"]:
                        if "labels" in metric:
                            if ("rank" in metric["labels"] and "target" in metric["labels"]):
                                rank = metric["labels"]["rank"]
                                target = metric["labels"]["target"]
                                if rank not in data[name][host]:
                                    data[name][host][rank] = {}
                                if target not in data[name][host][rank]:
                                    data[name][host][rank][target] = {}
                                data[name][host][rank][target] = \
                                    metric["value"]
                                self.log.info(
                                    "    %-12s %-4s %-6s %s",
                                    host, rank, target, metric["value"])
        return data

    def get_io_metrics(self, test_metrics=None):
        """Get the io telemetry metrics.

        Args:
            test_metrics (str list, optional): Comma-separated list of metric
                names to query. By default, test_metrics is entire
                ENGINE_IO_METRICS.

        Returns:
            dict: dictionary of dictionaries of container metric names and
                values per server host key

        """
        data = {}
        if test_metrics is None:
            test_metrics = self.ENGINE_IO_METRICS
        info = self.get_metrics(",".join(test_metrics))
        self.log.info("Telemetry Information")
        for name in test_metrics:
            for index, host in enumerate(info):
                if name in info[host]:
                    if index == 0:
                        self.log.info(
                            "  %s (%s):",
                            name, info[host][name]["description"])
                        self.log.info(
                            "    %-12s %-4s %-6s %-6s %s",
                            "Host", "Rank", "Target", "Size", "Value")
                    if name not in data:
                        data[name] = {}
                    if host not in data[name]:
                        data[name][host] = {}
                    for metric in info[host][name]["metrics"]:
                        if "labels" in metric:
                            if ("rank" in metric["labels"]
                                    and "target" in metric["labels"]
                                    and "size" in metric["labels"]):
                                rank = metric["labels"]["rank"]
                                target = metric["labels"]["target"]
                                size = metric["labels"]["size"]
                                if rank not in data[name][host]:
                                    data[name][host][rank] = {}
                                if target not in data[name][host][rank]:
                                    data[name][host][rank][target] = {}
                                data[name][host][rank][target][size] = \
                                    metric["value"]
                                self.log.info(
                                    "    %-12s %-4s %-6s %-6s %s",
                                    host, rank, target, size, metric["value"])
                            elif ("rank" in metric["labels"]
                                  and "target" in metric["labels"]):
                                rank = metric["labels"]["rank"]
                                target = metric["labels"]["target"]
                                if rank not in data[name][host]:
                                    data[name][host][rank] = {}
                                if target not in data[name][host][rank]:
                                    data[name][host][rank][target] = {}
                                data[name][host][rank][target]["-"] = \
                                    metric["value"]
                                self.log.info(
                                    "    %-12s %-4s %-6s %-6s %s",
                                    host, rank, target, "-", metric["value"])
        return data

    def check_container_metrics(self, open_count=None, create_count=None,
                                close_count=None, destroy_count=None,
                                query_count=None):
        """Verify the container telemetry metrics.

        Args:
            open_count (dict, optional): Number of cont_open operations per
                host key. Defaults to None.
            create_count (dict, optional): Number of cont_create per
                host key. Defaults to None.
            close_count (dict, optional): Number of cont_close operation per
                host key. Defaults to None.
            destroy_count (dict, optional): Number of cont_destroy operations
                per host key. Defaults to None.
            query_count (dict, optional): Number of cont_query operations
                per host key. Defaults to None.

        Returns:
            list: list of errors detected

        """
        errors = []
        expected = {
            "engine_pool_ops_cont_open": open_count,
            "engine_pool_ops_cont_close": close_count,
            "engine_pool_ops_cont_query": query_count,
            "engine_pool_ops_cont_create": create_count,
            "engine_pool_ops_cont_destroy": destroy_count,
        }
        data = self.get_container_metrics()
        for host in data:
            for name in expected:
                if name in data[host]:
                    if (expected[name] is not None
                            and host in expected[name]
                            and expected[name][host] != data[host][name]):
                        errors.append(
                            "{} mismatch on {}: expected={}; actual={}".format(
                                name, host, expected[name][host],
                                data[host][name]))
                else:
                    errors.append("No {} data for {}".format(name, host))
        return errors

    def get_nvme_metrics(self, server, specific_metrics=None):
        """Get the NVMe telemetry metrics.

        Args:
            specific_metrics(list): list of specific NVMe metrics
            server (DaosServerCommand): the server from which to determine what metrics
                                        will be available

        Returns:
            dict: dictionary of dictionaries of NVMe metric names and
                values per server host key

        """
        data = {}
        if specific_metrics is None:
            specific_metrics = self.ENGINE_NVME_METRICS

        # Add NVMe metrics for any NVMe devices configured for this server
        for nvme_list in server.manager.job.get_engine_values("bdev_list"):
            for nvme in nvme_list if nvme_list is not None else []:
                # Replace the '<id>' placeholder with the actual NVMe ID
                nvme_id = nvme.replace(":", "_").replace(".", "_")
                specific_metrics = [
                    name.replace("<id>", nvme_id)
                    for name in specific_metrics]

        info = self.get_metrics(",".join(specific_metrics))
        self.log.info("NVMe Telemetry Information")
        for name in specific_metrics:
            for index, host in enumerate(info):
                if name in info[host]:
                    if index == 0:
                        self.log.info(
                            "  %s (%s):",
                            name, info[host][name]["description"])
                        self.log.info(
                            "    %-12s %-4s %s",
                            "Host", "Rank", "Value")
                    if name not in data:
                        data[name] = {}
                    if host not in data[name]:
                        data[name][host] = {}
                    for metric in info[host][name]["metrics"]:
                        if "labels" in metric:
                            if "rank" in metric["labels"]:
                                rank = metric["labels"]["rank"]
                                if rank not in data[name][host]:
                                    data[name][host][rank] = {}
                                data[name][host][rank] = \
                                    metric["value"]
                                self.log.info(
                                    "    %-12s %-4s %s",
                                    host, rank, metric["value"])
        return data

    def verify_metric_value(self, metrics_data, min_value=None, max_value=None):
        """ Verify telemetry metrics from metrics_data.

        Args:
            metrics_data (dict): a dictionary of host keys linked to a list of metric names.
            min_value (int): minimum value of test metrics threshold, 0 if not set
            max_value (int): maximum value of test metrics threshold

            Returns:
                bool: True if all metrics are verified, False if any metrics are out of the
                      allowable range or less than 0
        """
        self.log.info("Verify threshold of metrics")
        status = True
        invalid = ""
        if min_value is None and max_value is None:
            # Verify that the metric value is >0 if a range is not provided
            min_value = 0

        for name in sorted(metrics_data):
            self.log.info("    --telemetry metric: %s", name)
            self.log.info("    %-12s %-4s %s", "Host", "Rank", "Value")
            for host in sorted(metrics_data[name]):
                for rank in sorted(metrics_data[name][host]):
                    value = metrics_data[name][host][rank]
                    invalid = "Metric value in range"
                    #Verify metrics are within allowable threshold
                    if min_value is not None and value < min_value:
                        status = False
                        invalid = "Metric value is smaller than {}: {}".format(min_value, value)
                    if max_value is not None and value > max_value:
                        status = False
                        invalid = "Metric value is larger than {}: {}".format(max_value, value)

                    self.log.info("    %-12s %-4s %s %s",
                                  host, rank, value, invalid)
        return status
