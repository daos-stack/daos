"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_utils import read_data, write_data
from telemetry_test_base import TestWithClientTelemetry


class DFSClientTelemetry(TestWithClientTelemetry):
    """Tests to verify DFS telemetry.

    :avocado: recursive
    """

    def test_dfs_metrics(self):
        """JIRA ID: DAOS-16837.

        Verify that the DFS metrics are incrementing as expected.
        After performing some I/O, the DFS-level metrics should look reasonable.

        Test steps:
        1) Create a pool and container
        2) Perform some I/O with IOR
        3) Verify that the DFS metrics are sane

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfs,telemetry
        :avocado: tags=DFSClientTelemetry,test_dfs_metrics
        """
        # create pool and container
        pool = self.get_pool(connect=True)
        container = self.get_container(pool=pool)

        self.log_step('Writing data to the pool (ior)')
        ior = write_data(self, container)
        self.log_step('Reading data from the pool (ior)')
        read_data(self, ior, container)

        # after an IOR run, we'd expect this set of metrics to have values > 0
        val_metric_names = [
            'client_dfs_ops_create',
            'client_dfs_ops_open',
            'client_dfs_ops_read',
            'client_dfs_ops_write'
        ]
        bkt_metric_names = [
            'client_dfs_read_bytes',
            'client_dfs_write_bytes'
        ]

        self.log_step('Reading dfs telemetry')
        after_metrics = self.telemetry.collect_client_data(val_metric_names + bkt_metric_names)
        for metric in val_metric_names:
            msum = sum(after_metrics[metric].values())
            self.assertGreater(msum, 0, f'{metric} value not greater than zero after I/O')
        for metric in bkt_metric_names:
            msum = sum(hist['sample_sum'] for hist in after_metrics[metric].values())
            self.assertGreater(msum, 0, f'{metric} sample_sum not greater than zero after I/O')

        self.log_step('Test passed')
