"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from telemetry_utils import TelemetryUtils
from test_utils_pool import time_pool_create

ENGINE_POOL_METRICS_SHORT = [
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
    "engine_pool_ops_key2anchor"
]


class DmgScale(TestWithServers):
    """Verify dmg commands works as expected in a large scale system.

    :avocado: recursive
    """

    def test_dmg_scale(self):
        """Run the following steps at Aurora and manually collect duration for each step.

        0. Format storage
        1. System query
        2. Create a 100% pool that spans all engines
        3. Pool query
        4. Pool destroy
        5. Create 50 pools spanning all the engines with each pool using a 1/50th of the capacity
        6. Pool list
        7. Query around 80 pool metrics
        8. Destroy all 50 pools
        9. System stop
        10. System start

        Jira ID: DAOS-10508.

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=deployment
        :avocado: tags=DmgScale,test_dmg_scale
        """
        # This is a manual test and we need to find the durations from job.log, so add "##" to make
        # it easy to search. The log is usually over 100K lines.
        self.log_step("## System query")
        dmg_command = self.get_dmg_command()
        dmg_command.system_query()

        self.log_step("## Create a 100% pool that spans all engines")
        pool = self.get_pool(namespace="/run/pool_1/*", create=False)
        duration = time_pool_create(log=self.log, number=1, pool=pool)
        self.log.info("## Single pool create duration = %.1f", duration)

        self.log_step("## Pool query")
        dmg_command.pool_query(pool.identifier)

        self.log_step("## Pool destroy")
        dmg_command.pool_destroy(pool.identifier, force=1)

        msg = ("## Create 50 pools spanning all the engines with each pool using a 1/50th of the "
               "capacity")
        self.log_step(msg)
        quantity = self.params.get("quantity", "/run/pool_50/*", 1)
        durations = []
        pools = []
        for count in range(quantity):
            pools.append(self.get_pool(namespace="/run/pool_50/*", create=False))
            durations.append(time_pool_create(log=self.log, number=count, pool=pools[-1]))
        self.log.info("## durations = %s", durations)
        total_duration = 0
        for duration in durations:
            total_duration += duration
        self.log.info("## 50 pools create duration = %.1f", total_duration)

        self.log_step("## Pool list")
        dmg_command.pool_list()

        self.log_step("## Query around 80 pool metrics")
        # To save time and logs, call telemetry on the first host only. With the 80 pool metrics
        # above, ~100K lines are printed per host.
        telemetry_utils = TelemetryUtils(
            dmg=dmg_command, servers=[self.server_managers[0].hosts[0]])
        telemetry_utils.get_metrics(name=",".join(ENGINE_POOL_METRICS_SHORT))

        self.log_step("## Destroy all 50 pools")
        self.destroy_pools(pools=pools)

        self.log_step("## System stop")
        dmg_command.system_stop(force=True)

        self.log_step("## System start")
        dmg_command.system_start()
