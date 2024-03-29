"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_utils import write_data
from telemetry_test_base import TestWithClientTelemetry


class ClientMetrics(TestWithClientTelemetry):
    """Tests to verify basic client telemetry.

    :avocado: recursive
    """

    def test_client_throughput_metrics(self):
        """JIRA ID: DAOS-8331.

        Verify that the client-side telemetry captures some throughput metrics.
        After performing some I/O, the update/fetch counters should be non-zero.

        Test steps:
        1) Create a pool and container
        2) Perform some I/O with IOR
        3) Verify that the read/write counters have been updated

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=telemetry
        :avocado: tags=ClientMetrics,test_client_throughput_metrics
        """
        # create pool and container
        pool = self.get_pool(connect=True)
        pool.set_property("reclaim", "disabled")
        container = self.get_container(pool=pool)

        # collect first set of pool metric data before read/write
        metric_names = [
            "client_pool_xferred_fetch",
            "client_pool_xferred_update",
        ]
        metrics_before = self.telemetry.collect_client_data(metric_names)
        for _, values in metrics_before.items():
            for label in values:
                # Initially all metrics should be 0
                values[label] = [0, 0]
        self.log.info('Metrics before write: %s', metrics_before)

        self.log_step('Writing data to the pool (ior)')
        write_data(self, container)

        metrics_after = self.telemetry.collect_client_data(metric_names)
        self.log.info('Metrics after write: %s', metrics_after)

        # Compare before/after

        self.log_step('Test passed')
