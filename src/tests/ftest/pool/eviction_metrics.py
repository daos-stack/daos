"""
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from mdtest_utils import get_mdtest, get_mdtest_container
from telemetry_test_base import TestWithTelemetry


class EvictionMetrics(TestWithTelemetry):
    """
    Tests DAOS client eviction from a pool that the client is using.

    :avocado: recursive
    """

    def test_eviction_metrics(self):
        """Verify page eviction on the pool

        1. Create a pool with a mem ratio of 100% (for pmem or phase 1) or 25% (for phase 2)
        2. Collect a baseline for the pool eviction metrics
        3. Run mdtest -a DFS to generate many small files larger than mem size
        4. Collect new page eviction metrics
        5. Verify page eviction

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=EvictionMetrics,test_eviction_metrics
        """
        evict_metrics = list(self.telemetry.ENGINE_POOL_VOS_CACHE_METRICS)

        self.log_step('Creating a pool (dmg pool create)')
        pool = self.get_pool(connect=False)

        self.log_step(
            'Collect pool eviction metrics after creating a pool (dmg telemetry metrics query)')
        expected_ranges = self.telemetry.collect_data(evict_metrics)
        for metric in sorted(expected_ranges):
            for label in expected_ranges[metric]:
                if self.server_managers[0].manager.job.using_control_metadata:
                    expected_ranges[metric][label] = [1, 1]
                else:
                    expected_ranges[metric][label] = [0, 0]

        self.log_step(
            'Verify pool eviction metrics after pool creation (dmg telemetry metrics query)')
        if not self.telemetry.verify_data(expected_ranges):
            self.fail('Pool eviction metrics verification failed after pool creation')

        self.log_step('Writing data to the pool (mdtest -a DFS)')
        mdtest = get_mdtest(self, self.hostlist_clients)
        container = get_mdtest_container(self, mdtest, pool)
        result = mdtest.run(pool, container, processes=16)
        if not result.passed:
            self.fail('Mdtest command failed')

        self.log_step(
            'Collect pool eviction metrics after writing data (dmg telemetry metrics query)')
        expected_ranges = self.telemetry.collect_data(evict_metrics)
        for metric in sorted(expected_ranges):
            for label in expected_ranges[metric]:
                if self.server_managers[0].manager.job.using_control_metadata:
                    expected_ranges[metric][label] = [1, 1]
                else:
                    expected_ranges[metric][label] = [0, 0]

        self.log_step(
            'Verify pool eviction metrics after writing data (dmg telemetry metrics query)')
        if not self.telemetry.verify_data(expected_ranges):
            self.fail('Pool eviction metrics verification failed after writing data')

        self.log_step('Test passed')
