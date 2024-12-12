"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_utils import read_data, write_data
from telemetry_test_base import TestWithClientTelemetry
from telemetry_utils import CLIENT_DFS_IO_METRICS, CLIENT_DFS_OPS_METRICS


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
        :avocado: tags=telemetry
        :avocado: tags=DFSClientTelemetry,test_dfs_metrics
        """
        # create pool and container
        pool = self.get_pool(connect=True)
        container = self.get_container(pool=pool)

        self.log_step('Writing data to the pool (ior)')
        ior = write_data(self, container)
        self.log_step('Reading data from the pool (ior)')
        read_data(self, ior, container)

        metric_names = CLIENT_DFS_OPS_METRICS + CLIENT_DFS_IO_METRICS

        self.log_step('Reading dfs telemetry')
        after_metrics = self.telemetry.collect_client_data(metric_names)
        for metric in metric_names:
            print(f'{metric}: {after_metrics[metric]}')

        self.log_step('Test passed')
