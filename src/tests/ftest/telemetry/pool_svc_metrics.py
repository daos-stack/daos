'''
  (C) Copyright 2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import time

from telemetry_test_base import TestWithTelemetry

MAP_VERSION_METRIC = "engine_pool_svc_map_version"
SVC_LEADER_METRIC = "engine_pool_svc_leader"
DEGRADED_RANKS_METRIC = "engine_pool_svc_degraded_ranks"


class PoolServiceMetrics(TestWithTelemetry):
    """Verify pool service metric values.

    :avocado: recursive
    """

    def collect_svc_telemetry(self, pool_uuid):
        """Collect the pool service metric values.

        Args:
            pool_uuid (str): The UUID of the pool to collect metrics from.

        Returns:
            dict: A dict of the current pool service leader's metrics.
        """
        def _map_version(rank_metrics):
            if rank_metrics is None or MAP_VERSION_METRIC not in rank_metrics:
                return 0
            return rank_metrics[MAP_VERSION_METRIC]

        def _pool_rank_metrics(host_metrics, pool_uuid):
            self.log.debug("collecting rank metrics for pool: %s", pool_uuid)
            pm = {}
            for hm in host_metrics.values():
                for k, v in hm.items():
                    try:
                        for m in v['metrics']:
                            if m['labels']['pool'].casefold() != str(pool_uuid).casefold():
                                continue
                            rank = m['labels']['rank']
                            if rank not in pm:
                                pm[rank] = {}
                            pm[rank][k] = m['value']
                    except KeyError:
                        continue
            return pm

        metrics_list = ",".join(self.telemetry.ENGINE_POOL_SVC_METRICS)
        host_metrics = self.telemetry.get_metrics(metrics_list)
        self.log.debug("host metrics: %s", host_metrics)
        pr_metrics = _pool_rank_metrics(host_metrics, pool_uuid)
        self.log.debug("pool rank metrics: %s", pr_metrics)
        leader_metrics = {}
        for metrics in pr_metrics.values():
            if _map_version(metrics) > _map_version(leader_metrics):
                leader_metrics = metrics

        self.log.debug("Pool service leader metrics: %s", leader_metrics)
        return leader_metrics

    def test_pool_service_metrics(self):
        """Test that pool service telemetry is updated as expected.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=telemetry
        :avocado: tags=PoolServiceMetrics,test_pool_service_metrics
        """
        self.log_step("Create pool for testing.")
        pool = self.get_pool(connect=True)

        self.log_step("Collect pool service metrics prior to making changes.")
        initial_metrics = self.collect_svc_telemetry(pool.uuid)
        self.assertTrue(MAP_VERSION_METRIC in initial_metrics,
                        f"initial metrics don't contain {MAP_VERSION_METRIC} (no leader?)")
        self.assertTrue(initial_metrics[MAP_VERSION_METRIC] == 1,
                        "initial pool service map version is not 1")
        self.assertTrue(initial_metrics[DEGRADED_RANKS_METRIC] == 0,
                        "initial pool service degraded rank count is not 0")

        restart_rank = initial_metrics[SVC_LEADER_METRIC]
        self.log_step(f"Stop pool service leader rank: {restart_rank}")
        self.server_managers[0].stop_ranks(ranks=[restart_rank])

        self.log_step("Verify the pool service leader rank has stopped successfully.")
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[restart_rank], valid_states=["stopped", "excluded"], max_checks=15)
        if failed_ranks:
            self.fail(f"Rank {restart_rank} didn't stop!")

        def _wait_for_telemetry(test):
            metrics = self.collect_svc_telemetry(pool.uuid)
            while True:
                try:
                    if test(metrics):
                        return metrics
                except KeyError:
                    pass

                self.log.info("waiting for pool telemetry to update")
                time.sleep(5)
                metrics = self.collect_svc_telemetry(pool.uuid)

        self.log_step("Wait for rank exclusion to show up in the telemetry.")
        metrics = _wait_for_telemetry(lambda m: m[MAP_VERSION_METRIC] > 1)

        self.log_step("Verify that the pool service telemetry has updated.")
        self.assertTrue(metrics[DEGRADED_RANKS_METRIC] == 1,
                        "pool service degraded rank count should be 1")

        self.log_step("Restart the stopped rank.")
        self.server_managers[0].start_ranks(ranks=[restart_rank])

        self.log_step("Verify the desired rank restarted successfully.")
        failed_ranks = self.server_managers[0].check_rank_state(
            ranks=[restart_rank], valid_states=["joined"], max_checks=15)
        if failed_ranks:
            self.fail(f"Rank {restart_rank} didn't start!")

        self.log_step("Reintegrate failed rank back into the pool.")
        pool.reintegrate(restart_rank)

        self.log_step("Wait for reintegration to show up in the telemetry.")
        metrics = _wait_for_telemetry(lambda m: m[DEGRADED_RANKS_METRIC] == 0)

        self.log_step("Test passed.")
