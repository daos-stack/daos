"""
(C) Copyright 2021-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
import re
from ClusterShell.NodeSet import NodeSet


class TelemetryUtils():
    # pylint: disable=too-many-nested-blocks
    """Defines a object used to verify telemetry information."""

    # Define a set of patterns that shouldn't be used for comparisons.
    METRIC_EXCLUDE_PATTERNS = [
        re.compile("^go_.*"),       # internal Go metrics
        re.compile("^process_.*"),  # internal process metrics
    ]
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
        "engine_pool_ops_pool_connect",
        "engine_pool_ops_pool_disconnect",
        "engine_pool_ops_pool_evict",
        "engine_pool_ops_pool_query",
        "engine_pool_ops_pool_query_space",
        "engine_pool_resent",
        "engine_pool_restarted",
        "engine_pool_retry",
        "engine_pool_scrubber_busy_time",
        "engine_pool_scrubber_bytes_scrubbed_current",
        "engine_pool_scrubber_bytes_scrubbed_prev",
        "engine_pool_scrubber_bytes_scrubbed_total",
        "engine_pool_scrubber_corruption_current",
        "engine_pool_scrubber_corruption_total",
        "engine_pool_scrubber_csums_current",
        "engine_pool_scrubber_csums_prev",
        "engine_pool_scrubber_csums_total",
        "engine_pool_scrubber_next_csum_scrub",
        "engine_pool_scrubber_next_tree_scrub",
        "engine_pool_scrubber_prev_duration",
        "engine_pool_scrubber_prev_duration_max",
        "engine_pool_scrubber_prev_duration_mean",
        "engine_pool_scrubber_prev_duration_min",
        "engine_pool_scrubber_prev_duration_stddev",
        "engine_pool_scrubber_scrubber_started",
        "engine_pool_scrubber_scrubs_completed",
        "engine_pool_started_at",
        "engine_pool_vos_aggregation_akey_deleted",
        "engine_pool_vos_aggregation_akey_scanned",
        "engine_pool_vos_aggregation_akey_skipped",
        "engine_pool_vos_aggregation_csum_errors",
        "engine_pool_vos_aggregation_deleted_ev",
        "engine_pool_vos_aggregation_deleted_sv",
        "engine_pool_vos_aggregation_dkey_deleted",
        "engine_pool_vos_aggregation_dkey_scanned",
        "engine_pool_vos_aggregation_dkey_skipped",
        "engine_pool_vos_aggregation_epr_duration",
        "engine_pool_vos_aggregation_epr_duration_max",
        "engine_pool_vos_aggregation_epr_duration_mean",
        "engine_pool_vos_aggregation_epr_duration_min",
        "engine_pool_vos_aggregation_epr_duration_stddev",
        "engine_pool_vos_aggregation_merged_recs",
        "engine_pool_vos_aggregation_merged_size",
        "engine_pool_vos_aggregation_obj_deleted",
        "engine_pool_vos_aggregation_obj_scanned",
        "engine_pool_vos_aggregation_obj_skipped",
        "engine_pool_vos_aggregation_uncommitted",
        "engine_pool_vos_space_nvme_used",
        "engine_pool_vos_space_scm_used",
        "engine_pool_xferred_fetch",
        "engine_pool_xferred_update",
        "engine_pool_EC_update_full_stripe",
        "engine_pool_EC_update_partial",
        "engine_pool_block_allocator_alloc_hint",
        "engine_pool_block_allocator_alloc_large",
        "engine_pool_block_allocator_alloc_small",
        "engine_pool_block_allocator_frags_aging",
        "engine_pool_block_allocator_frags_large",
        "engine_pool_block_allocator_frags_small",
        "engine_pool_block_allocator_free_blks",
        "engine_pool_ops_key2anchor"]
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
    ENGINE_DMABUFF_METRICS = [
        "engine_dmabuff_total_chunks",
        "engine_dmabuff_used_chunks_io",
        "engine_dmabuff_used_chunks_local",
        "engine_dmabuff_used_chunks_rebuild",
        "engine_dmabuff_bulk_grps",
        "engine_dmabuff_active_reqs",
        "engine_dmabuff_queued_reqs",
        "engine_dmabuff_grab_errs",
        "engine_dmabuff_grab_retries",
        "engine_dmabuff_grab_retries_max",
        "engine_dmabuff_grab_retries_mean",
        "engine_dmabuff_grab_retries_min",
        "engine_dmabuff_grab_retries_stddev",
        "engine_dmabuff_wal_sz",
        "engine_dmabuff_wal_sz_max",
        "engine_dmabuff_wal_sz_mean",
        "engine_dmabuff_wal_sz_min",
        "engine_dmabuff_wal_sz_stddev",
        "engine_dmabuff_wal_qd",
        "engine_dmabuff_wal_qd_max",
        "engine_dmabuff_wal_qd_mean",
        "engine_dmabuff_wal_qd_min",
        "engine_dmabuff_wal_qd_stddev",
        "engine_dmabuff_wal_waiters",
        "engine_dmabuff_wal_waiters_max",
        "engine_dmabuff_wal_waiters_mean",
        "engine_dmabuff_wal_waiters_min",
        "engine_dmabuff_wal_waiters_stddev"]
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
    ENGINE_IO_LATENCY_BULK_FETCH_METRICS = [
        "engine_io_latency_bulk_fetch",
        "engine_io_latency_bulk_fetch_max",
        "engine_io_latency_bulk_fetch_mean",
        "engine_io_latency_bulk_fetch_min",
        "engine_io_latency_bulk_fetch_stddev"]
    ENGINE_IO_LATENCY_VOS_FETCH_METRICS = [
        "engine_io_latency_vos_fetch",
        "engine_io_latency_vos_fetch_max",
        "engine_io_latency_vos_fetch_mean",
        "engine_io_latency_vos_fetch_min",
        "engine_io_latency_vos_fetch_stddev"]
    ENGINE_IO_LATENCY_BIO_FETCH_METRICS = [
        "engine_io_latency_bio_fetch",
        "engine_io_latency_bio_fetch_max",
        "engine_io_latency_bio_fetch_mean",
        "engine_io_latency_bio_fetch_min",
        "engine_io_latency_bio_fetch_stddev"]
    ENGINE_IO_LATENCY_UPDATE_METRICS = [
        "engine_io_latency_update",
        "engine_io_latency_update_max",
        "engine_io_latency_update_mean",
        "engine_io_latency_update_min",
        "engine_io_latency_update_stddev"]
    ENGINE_IO_LATENCY_TGT_UPDATE_METRICS = [
        "engine_io_latency_tgt_update",
        "engine_io_latency_tgt_update_max",
        "engine_io_latency_tgt_update_mean",
        "engine_io_latency_tgt_update_min",
        "engine_io_latency_tgt_update_stddev"]
    ENGINE_IO_LATENCY_BULK_UPDATE_METRICS = [
        "engine_io_latency_bulk_update",
        "engine_io_latency_bulk_update_max",
        "engine_io_latency_bulk_update_mean",
        "engine_io_latency_bulk_update_min",
        "engine_io_latency_bulk_update_stddev"]
    ENGINE_IO_LATENCY_VOS_UPDATE_METRICS = [
        "engine_io_latency_vos_update",
        "engine_io_latency_vos_update_max",
        "engine_io_latency_vos_update_mean",
        "engine_io_latency_vos_update_min",
        "engine_io_latency_vos_update_stddev"]
    ENGINE_IO_LATENCY_BIO_UPDATE_METRICS = [
        "engine_io_latency_bio_update",
        "engine_io_latency_bio_update_max",
        "engine_io_latency_bio_update_mean",
        "engine_io_latency_bio_update_min",
        "engine_io_latency_bio_update_stddev"]
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
    ENGINE_IO_OPS_KEY2ANCHOR_ACTIVE_METRICS = [
        "engine_io_ops_key2anchor_active",
        "engine_io_ops_key2anchor_active_max",
        "engine_io_ops_key2anchor_active_mean",
        "engine_io_ops_key2anchor_active_min",
        "engine_io_ops_key2anchor_active_stddev"]
    ENGINE_IO_OPS_KEY2ANCHOR_LATENCY_METRICS = [
        "engine_io_ops_key2anchor_latency",
        "engine_io_ops_key2anchor_latency_max",
        "engine_io_ops_key2anchor_latency_mean",
        "engine_io_ops_key2anchor_latency_min",
        "engine_io_ops_key2anchor_latency_stddev"]
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
        ENGINE_IO_LATENCY_BULK_FETCH_METRICS +\
        ENGINE_IO_LATENCY_VOS_FETCH_METRICS +\
        ENGINE_IO_LATENCY_BIO_FETCH_METRICS +\
        ENGINE_IO_LATENCY_UPDATE_METRICS +\
        ENGINE_IO_LATENCY_TGT_UPDATE_METRICS +\
        ENGINE_IO_LATENCY_BULK_UPDATE_METRICS +\
        ENGINE_IO_LATENCY_VOS_UPDATE_METRICS +\
        ENGINE_IO_LATENCY_BIO_UPDATE_METRICS +\
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
        ENGINE_IO_OPS_KEY2ANCHOR_ACTIVE_METRICS +\
        ENGINE_IO_OPS_KEY2ANCHOR_LATENCY_METRICS +\
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
        "engine_net_failed_addr",
        "engine_net_req_timeout",
        "engine_net_uri_lookup_timeout",
        "engine_net_uri_lookup_other",
        "engine_net_uri_lookup_self"]
    ENGINE_RANK_METRICS = [
        "engine_rank"]
    ENGINE_NVME_HEALTH_METRICS = [
        "engine_nvme_commands_data_units_written",
        "engine_nvme_commands_data_units_read",
        "engine_nvme_commands_host_write_cmds",
        "engine_nvme_commands_host_read_cmds",
        "engine_nvme_commands_media_errs",
        "engine_nvme_commands_read_errs",
        "engine_nvme_commands_write_errs",
        "engine_nvme_commands_unmap_errs",
        "engine_nvme_commands_checksum_mismatch",
        "engine_nvme_power_cycles",
        "engine_nvme_commands_ctrl_busy_time",
        "engine_nvme_power_on_hours",
        "engine_nvme_unsafe_shutdowns"]
    ENGINE_NVME_TEMP_METRICS = [
        "engine_nvme_temp_current"]
    ENGINE_NVME_TEMP_TIME_METRICS = [
        "engine_nvme_temp_warn_time",
        "engine_nvme_temp_crit_time"]
    ENGINE_NVME_RELIABILITY_METRICS = [
        "engine_nvme_reliability_avail_spare",
        "engine_nvme_reliability_avail_spare_threshold"]
    ENGINE_NVME_CRIT_WARN_METRICS = [
        "engine_nvme_reliability_avail_spare_warn",
        "engine_nvme_reliability_reliability_warn",
        "engine_nvme_temp_warn",
        "engine_nvme_read_only_warn",
        "engine_nvme_volatile_mem_warn"]
    ENGINE_NVME_INTEL_VENDOR_METRICS = [
        "engine_nvme_vendor_program_fail_cnt_norm",
        "engine_nvme_vendor_program_fail_cnt_raw",
        "engine_nvme_vendor_erase_fail_cnt_norm",
        "engine_nvme_vendor_erase_fail_cnt_raw",
        "engine_nvme_vendor_wear_leveling_cnt_norm",
        "engine_nvme_vendor_wear_leveling_cnt_min",
        "engine_nvme_vendor_wear_leveling_cnt_max",
        "engine_nvme_vendor_wear_leveling_cnt_avg",
        "engine_nvme_vendor_endtoend_err_cnt_raw",
        "engine_nvme_vendor_crc_err_cnt_raw",
        "engine_nvme_vendor_media_wear_raw",
        "engine_nvme_vendor_host_reads_raw",
        "engine_nvme_vendor_crc_workload_timer_raw",
        "engine_nvme_vendor_thermal_throttle_status_raw",
        "engine_nvme_vendor_thermal_throttle_event_cnt",
        "engine_nvme_vendor_retry_buffer_overflow_cnt",
        "engine_nvme_vendor_pll_lock_loss_cnt",
        "engine_nvme_vendor_nand_bytes_written",
        "engine_nvme_vendor_host_bytes_written"]
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
        all_metrics_names.extend(self.ENGINE_NET_METRICS)
        all_metrics_names.extend(self.ENGINE_RANK_METRICS)
        all_metrics_names.extend(self.ENGINE_DMABUFF_METRICS)
        if with_pools:
            all_metrics_names.extend(self.ENGINE_POOL_METRICS)
            all_metrics_names.extend(self.ENGINE_CONTAINER_METRICS)

        # Add the NVMe metrics if the test is run on a hardware cluster.
        for nvme_list in server.manager.job.get_engine_values("bdev_list"):
            if nvme_list and len(nvme_list) > 0:
                all_metrics_names.extend(self.ENGINE_NVME_METRICS)
                break

        return all_metrics_names

    def is_excluded_metric(self, name):
        """Check if the given metric is excluded.

        Args:
            name (str): the metric name to check

        Returns:
            bool: True if the metric is excluded, False otherwise

        """
        for pat in self.METRIC_EXCLUDE_PATTERNS:
            if pat.match(name):
                return True
        return False

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
                        if "name" in entry and not self.is_excluded_metric(entry["name"]):
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
        for host, host_info in info.items():
            data[host] = {name: 0 for name in self.ENGINE_CONTAINER_METRICS}
            for name in self.ENGINE_CONTAINER_METRICS:
                if name in host_info:
                    for metric in host_info[name]["metrics"]:
                        self.log.info(
                            "  %s (%s): %s (%s)",
                            host_info[name]["description"], name,
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
        for host, host_data in data.items():
            for name, count in expected.items():
                if name in host_data:
                    if (count is not None and host in count and count[host] != host_data[name]):
                        errors.append(
                            "{} mismatch on {}: expected={}; actual={}".format(
                                name, host, count[host], host_data[name]))
                else:
                    errors.append("No {} data for {}".format(name, host))
        return errors

    def get_nvme_metrics(self, specific_metrics=None):
        """Get the NVMe telemetry metrics.

        Args:
            specific_metrics(list): list of specific NVMe metrics

        Returns:
            dict: dictionary of dictionaries of NVMe metric names and
                values per server host key

        """
        data = {}
        if specific_metrics is None:
            specific_metrics = self.ENGINE_NVME_METRICS

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
                    # Verify metrics are within allowable threshold
                    if min_value is not None and value < min_value:
                        status = False
                        invalid = "Metric value is smaller than {}: {}".format(min_value, value)
                    if max_value is not None and value > max_value:
                        status = False
                        invalid = "Metric value is larger than {}: {}".format(max_value, value)

                    self.log.info("    %-12s %-4s %s %s",
                                  host, rank, value, invalid)
        return status
