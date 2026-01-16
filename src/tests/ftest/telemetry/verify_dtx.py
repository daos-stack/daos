"""
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from job_manager_utils import get_job_manager
from mdtest_utils import MDTEST_NAMESPACE, run_mdtest
from telemetry_test_base import TestWithTelemetry


class VerifyDTX(TestWithTelemetry):
    """
    Ensures DTX is involved with MD on SSD phase 2 pool.

    :avocado: recursive
    """

    def test_verify_dtx(self):
        """Ensure DTX is involved with MD on SSD phase 2 pool.

        1. Create a pool with a mem ratio of 100% (for pmem or phase 1) or 25% (for phase 2)
        2. Collect a baseline for the DTX metrics
        3. Run mdtest -a DFS to write data with different object classes
        4. Collect new DTX metrics
        5. Verify DTX metrics

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=pool
        :avocado: tags=VerifyDTX,test_verify_dtx
        """
        dtx_metrics = list(self.telemetry.ENGINE_POOL_VOS_CACHE_METRICS[:1])
        dtx_metrics += list(self.telemetry.ENGINE_IO_DTX_COMMITTED_METRICS)

        self.log_step('Creating a pool (dmg pool create)')
        pool = self.get_pool(connect=False)

        self.log_step('Collect DTX metrics after creating a pool (dmg telemetry metrics query)')
        expected_ranges = self.telemetry.collect_data(dtx_metrics)
        for metric in sorted(expected_ranges):
            for label in expected_ranges[metric]:
                expected_ranges[metric][label] = [0, 0]
        self.log.debug('%s expected_ranges: %s', pool, expected_ranges)

        self.log_step('Verify DTX metrics after pool creation')
        if not self.telemetry.verify_data(expected_ranges):
            self.fail('DTX metrics verification failed after pool creation')

        object_classes = self.params.get('object_classes', '/run/*')
        manager = get_job_manager(self, subprocess=False, timeout=None)
        processes = self.params.get('processes', MDTEST_NAMESPACE, None)
        ppn = self.params.get('ppn', MDTEST_NAMESPACE, None)
        for oclass in object_classes:
            self.log_step(f'Write data into a containers with the {oclass} object classes (mdtest)')
            container = self.get_container(pool, oclass=oclass, dir_oclass=oclass)
            run_mdtest(
                self, self.hostlist_clients, self.workdir, None, container, processes, ppn, manager,
                mdtest_params={'dfs_oclass': oclass, 'dfs_dir_oclass': oclass})

        self.log_step('Collect DTX metrics after writing data (dmg telemetry metrics query)')
        expected_ranges = self.telemetry.collect_data(dtx_metrics)
        for metric in sorted(expected_ranges):
            for label in expected_ranges[metric]:
                if pool.mem_ratio.value is None:
                    expected_ranges[metric][label] = [0, 0]
                else:
                    expected_ranges[metric][label] = [1]
        self.log.debug('%s expected_ranges: %s', pool, expected_ranges)

        self.log_step('Verify DTX metrics after writing data')
        if not self.telemetry.verify_data(expected_ranges):
            self.fail('DTX metrics verification failed after writing data')

        self.log_step('Test passed')
