"""
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import json
import math

from job_manager_utils import get_job_manager
from mdtest_utils import MDTEST_NAMESPACE, run_mdtest
from telemetry_test_base import TestWithTelemetry


class VerifyDTXMetrics(TestWithTelemetry):
    """
    Ensures DTX is involved with MD on SSD phase 2 pool.

    :avocado: recursive
    """

    def test_verify_dtx_metrics(self):
        """Ensure DTX is involved with MD on SSD phase 2 pool.

        1. Create a pool with a mem ratio of 100% (for pmem or phase 1) or 25% (for phase 2)
        2. Collect a baseline for the DTX metrics
        3. Run mdtest -a DFS to write data with different object classes
        4. Collect new DTX metrics
        5. Verify DTX metrics

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=hw,medium
        :avocado: tags=vm
        :avocado: tags=pool
        :avocado: tags=VerifyDTXMetrics,test_verify_dtx_metrics
        """
        # pylint: disable=too-many-branches
        write_bytes = self.params.get('write_bytes', MDTEST_NAMESPACE, None)
        processes = self.params.get('processes', MDTEST_NAMESPACE, None)
        ppn = self.params.get('ppn', MDTEST_NAMESPACE, None)
        object_classes = self.params.get('object_classes', '/run/*')

        dtx_metrics = list(self.telemetry.ENGINE_POOL_VOS_CACHE_METRICS[:1])
        dtx_metrics += list(self.telemetry.ENGINE_IO_DTX_COMMITTED_METRICS)

        self.log_step('Creating a pool (dmg pool create)')
        pool = self.get_pool(connect=False)
        try:
            _result = json.loads(pool.dmg.result.stdout)
            tier_bytes_scm = int(_result['response']['tier_bytes'][0])
            mem_file_bytes = int(_result['response']['mem_file_bytes'])
            total_engines = len(_result['response']['tgt_ranks'])
        except Exception as error:      # pylint: disable=broad-except
            self.fail(f'Error extracting data for dmg pool create output: {error}')

        # Calculate the mdtest files_per_process based upon the scm size and other mdtest params
        _write_procs = processes
        _mdtest_cmds = len(object_classes)
        if ppn is not None:
            _write_procs = ppn * len(self.host_info.clients.hosts)
        files_per_process = math.floor(mem_file_bytes / (write_bytes * _write_procs * _mdtest_cmds))
        if tier_bytes_scm > mem_file_bytes:
            # Write more (225%) files to exceed mem_file_bytes and cause eviction
            num_of_files_dirs = math.ceil(files_per_process * 2.25)
        else:
            # Write less (75%) files to avoid out of space errors
            num_of_files_dirs = math.floor(files_per_process * 0.75)

        self.log.debug("-" * 60)
        self.log.debug("Pool %s create data:", pool)
        self.log.debug("  tier_bytes_scm (per engine/total):   %s / %s",
                       tier_bytes_scm, tier_bytes_scm * total_engines)
        self.log.debug("  mem_file_bytes (per engine/total):   %s / %s",
                       mem_file_bytes, mem_file_bytes * total_engines)
        self.log.debug("  mem_ratio.value:                     %s", pool.mem_ratio.value)
        self.log.debug("  total_engines:                       %s", total_engines)
        self.log.debug("Mdtest write parameters:")
        self.log.debug("  write_bytes per mdtest:              %s", write_bytes)
        if ppn is not None:
            self.log.debug("  processes (ppn * nodes):             %s * %s = %s",
                           ppn, len(self.host_info.clients.hosts), _write_procs)
        else:
            self.log.debug("  processes:                           %s", processes)
        self.log.debug("  files_per_process per mtest:         %s", files_per_process)
        self.log.debug("  number of mdtest commands:           %s", _mdtest_cmds)
        self.log.debug("  num_of_files_dirs per mdtest:        %s", num_of_files_dirs)
        self.log.debug("  total expected to write:             %s",
                       _mdtest_cmds * _write_procs * write_bytes * num_of_files_dirs)
        self.log.debug("-" * 60)

        self.log_step('Collect DTX metrics after creating a pool (dmg telemetry metrics query)')
        expected_ranges = self.telemetry.collect_data(dtx_metrics)
        for metric in expected_ranges:
            for label in expected_ranges[metric]:
                expected_ranges[metric][label] = [0, 0]             # 0 only
                if pool.mem_ratio.value is not None:
                    suffixes = [
                        '_dtx_committed_max',
                        '_dtx_committed_mean',
                        '_dtx_committed_samples',
                        '_dtx_committed_stddev',
                        '_dtx_committed_sum',
                        '_dtx_committed_sumsquares'
                    ]
                    if any(map(metric.endswith, suffixes)):
                        expected_ranges[metric][label] = [0]        # 0 or greater (phase 2)
        self.log.debug('%s expected_ranges: %s', pool, expected_ranges)

        self.log_step('Verify DTX metrics after pool creation')
        if not self.telemetry.verify_data(expected_ranges):
            self.fail('DTX metrics verification failed after pool creation')

        manager = get_job_manager(self, subprocess=False, timeout=None)
        processes = self.params.get('processes', MDTEST_NAMESPACE, None)
        ppn = self.params.get('ppn', MDTEST_NAMESPACE, None)
        for oclass in object_classes:
            self.log_step(f'Write data into a containers with the {oclass} object classes (mdtest)')
            container = self.get_container(pool, oclass=oclass, dir_oclass=oclass)
            run_mdtest(
                self, self.hostlist_clients, self.workdir, None, container, processes, ppn, manager,
                mdtest_params={'dfs_oclass': oclass, 'dfs_dir_oclass': oclass,
                               'num_of_files_dirs': num_of_files_dirs})

        self.log_step('Collect DTX metrics after writing data (dmg telemetry metrics query)')
        expected_ranges = self.telemetry.collect_data(dtx_metrics)
        for metric in expected_ranges:
            for label in expected_ranges[metric]:
                if metric.endswith('_dtx_committed'):
                    expected_ranges[metric][label] = [0]            # 0 or greater
                elif metric.endswith('_dtx_committed_max'):
                    expected_ranges[metric][label] = [100]          # 100 or greater
                elif metric.endswith('_dtx_committed_mean'):
                    expected_ranges[metric][label] = [50]           # 50 or greater
                elif metric.endswith('_dtx_committed_min'):
                    expected_ranges[metric][label] = [0]            # 0 or greater
                elif metric.endswith('_dtx_committed_sum'):
                    expected_ranges[metric][label] = [1000]         # 1000 or greater
                elif metric.endswith('_dtx_committed_sumsquares'):
                    expected_ranges[metric][label] = [100000]       # 100,000 or greater
                elif metric.endswith('_vos_cache_page_evict'):
                    if pool.mem_ratio.value is None:
                        expected_ranges[metric][label] = [0, 0]     # 0 only (phase 1)
                    else:
                        expected_ranges[metric][label] = [1]        # 1 or greater (phase 2)
                else:
                    # e.g. *_dtx_committed_samples, *_dtx_committed_stddev
                    expected_ranges[metric][label] = [1]            # 1 or greater
        self.log.debug('%s expected_ranges: %s', pool, expected_ranges)

        self.log_step('Verify DTX metrics after writing data')
        if not self.telemetry.verify_data(expected_ranges):
            self.fail('DTX metrics verification failed after writing data')

        self.log_step('Test passed')
