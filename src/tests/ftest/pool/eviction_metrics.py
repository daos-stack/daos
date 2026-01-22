"""
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import json
import math

from job_manager_utils import get_job_manager
from mdtest_utils import MDTEST_NAMESPACE, run_mdtest
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
        write_bytes = self.params.get('write_bytes', MDTEST_NAMESPACE, None)
        processes = self.params.get('processes', MDTEST_NAMESPACE, None)
        ppn = self.params.get('ppn', MDTEST_NAMESPACE, None)

        evict_metrics = list(self.telemetry.ENGINE_POOL_VOS_CACHE_METRICS)

        self.log_step('Creating a pool (dmg pool create)')
        pool = self.get_pool(connect=False)
        try:
            _result = json.loads(pool.dmg.result.stdout)
            tier_bytes_scm = int(_result["response"]["tier_bytes"][0])
            mem_file_bytes = int(_result["response"]["mem_file_bytes"])
        except Exception as error:      # pylint: disable=broad-except
            self.fail(f"Error extracting data for dmg pool create output: {error}")

        # Calculate the mdtest files_per_process based upon the scm size and other mdtest params
        _write_processes = processes
        if ppn is not None:
            _write_processes = ppn * len(self.host_info.clients.hosts)
        files_per_process = math.floor(mem_file_bytes / (write_bytes * _write_processes))
        if tier_bytes_scm > mem_file_bytes:
            # Write more (110%) files to exceed mem_file_bytes and cause eviction
            mdtest_params = {"num_of_files_dirs": math.ceil(files_per_process * 1.10)}
        else:
            # Write less (30%) files to avoid out of space errors
            mdtest_params = {"num_of_files_dirs": math.floor(files_per_process * 0.30)}

        self.log.debug("-" * 60)
        self.log.debug("Pool %s create data:", pool)
        self.log.debug("  tier_bytes_scm:     %s", tier_bytes_scm)
        self.log.debug("  mem_file_bytes:     %s", mem_file_bytes)
        self.log.debug("  mem_ratio.value:    %s", pool.mem_ratio.value)
        self.log.debug("Mdtest write parameters:")
        self.log.debug("  write_bytes:        %s", write_bytes)
        if ppn is not None:
            self.log.debug("  ppn / nodes:        %s / %s", ppn, len(self.host_info.clients.hosts))
        else:
            self.log.debug("  processes:          %s", processes)
        self.log.debug("  files_per_process:  %s", files_per_process)
        self.log.debug("  num_of_files_dirs:  %s", mdtest_params["num_of_files_dirs"])
        self.log.debug("  expected to write:  %s",
                       _write_processes * write_bytes * mdtest_params["num_of_files_dirs"])
        self.log.debug("-" * 60)

        self.log_step('Creating a container (dmg container create)')
        container = self.get_container(pool)

        self.log_step(
            'Collect pool eviction metrics after creating a pool (dmg telemetry metrics query)')
        expected_ranges = self.telemetry.collect_data(evict_metrics)
        for metric in sorted(expected_ranges):
            for label in expected_ranges[metric]:
                if pool.mem_ratio.value is not None and metric.endswith('_hit'):
                    expected_ranges[metric][label] = [0, 100]           # 0-100 (phase 2)
                elif pool.mem_ratio.value is not None and metric.endswith('_miss'):
                    expected_ranges[metric][label] = [0, 5]             # 0-5 (phase 2)
                elif pool.mem_ratio.value is not None and metric.endswith('_ne'):
                    expected_ranges[metric][label] = [0, 5]             # 0-5 (phase 2)
                else:
                    expected_ranges[metric][label] = [0, 0]             # 0 only
        self.log.debug("%s expected_ranges: %s", pool, expected_ranges)

        self.log_step('Verify pool eviction metrics after pool creation')
        if not self.telemetry.verify_data(expected_ranges):
            self.fail('Pool eviction metrics verification failed after pool creation')

        self.log_step('Writing data to the pool (mdtest -a DFS)')
        manager = get_job_manager(self, subprocess=False, timeout=None)
        run_mdtest(
            self, self.hostlist_clients, self.workdir, None, container, processes, ppn, manager,
            mdtest_params=mdtest_params)

        self.log_step(
            'Collect pool eviction metrics after writing data (dmg telemetry metrics query)')
        expected_ranges = self.telemetry.collect_data(evict_metrics)
        for metric in sorted(expected_ranges):
            for label in expected_ranges[metric]:
                if pool.mem_ratio.value is None:
                    expected_ranges[metric][label] = [0, 0]             # 0 only (phase 1)
                elif metric.endswith('_page_flush'):
                    expected_ranges[metric][label] = [0]                # 0 or greater (phase 2)
                else:
                    expected_ranges[metric][label] = [1]                # 1 or greater (phase 2)
        self.log.debug("%s expected_ranges: %s", pool, expected_ranges)

        self.log_step('Verify pool eviction metrics after writing data')
        if not self.telemetry.verify_data(expected_ranges):
            self.fail('Pool eviction metrics verification failed after writing data')

        self.log_step('Test passed')
