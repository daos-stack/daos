"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_utils import read_data, write_data
from telemetry_test_base import TestWithClientTelemetry


class BasicClientTelemetry(TestWithClientTelemetry):
    """Tests to verify basic client telemetry.

    :avocado: recursive
    """

    def test_client_metrics_exist(self):
        """JIRA ID: DAOS-8331.

        Verify that the client-side telemetry captures some throughput metrics.
        After performing some I/O, there should be some client telemetry data.

        Test steps:
        1) Create a pool and container
        2) Perform some I/O with IOR
        3) Verify that there is some client telemetry data

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=telemetry
        :avocado: tags=BasicClientTelemetry,test_client_metrics_exist
        """
        # create pool and container
        pool = self.get_pool(connect=True)
        container = self.get_container(pool=pool)

        self.log_step('Writing data to the pool (ior)')
        ior = write_data(self, container)
        self.log_step('Reading data from the pool (ior)')
        read_data(self, ior, container)

        metric_names = [
            "client_pool_xferred_fetch",
            "client_pool_xferred_update",
        ]

        self.log_step('Reading client telemetry (reads & writes should be > 0)')
        after_metrics = self.telemetry.collect_client_data(metric_names)
        for metric in metric_names:
            msum = 0
            for value in after_metrics[metric].values():
                msum += value
            self.assertGreater(msum, 0)

        self.log_step('Test passed')
