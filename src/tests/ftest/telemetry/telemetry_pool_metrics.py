#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail
from ior_test_base import IorTestBase
from telemetry_test_base import TestWithTelemetry


class TelemetryPoolMetrics(IorTestBase, TestWithTelemetry):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Test telemetry pool basic metrics.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TelemetryPoolMetrics object."""
        super().__init__(*args, **kwargs)

        self.dfs_oclass = None
        self.ior_transfer_size = None
        self.ior_block_size = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        self.dfs_oclass = self.params.get("oclass", '/run/container/*', self.dfs_oclass)
        self.ior_transfer_size = self.params.get(
            "transfer_size", '/run/ior/*', self.ior_transfer_size)
        self.ior_block_size = self.params.get("block_size", '/run/ior/*', self.ior_block_size)

    def get_expected_values(self, resent_ops):
        """Return the expected metrics value output"""
        ops_number = int(self.ior_block_size / self.ior_transfer_size)
        pool_ops_minmax = {
            "SX": {
                "engine_pool_ops_fetch": (
                    ops_number + 7,
                    ops_number + 8
                ),
                "engine_pool_ops_update": (
                    ops_number + 1,
                    ops_number + resent_ops + 2
                ),
                "engine_pool_xferred_fetch": (
                    ops_number * self.ior_transfer_size,
                    (ops_number + 1) * self.ior_transfer_size
                ),
                "engine_pool_xferred_update": (
                    ops_number * self.ior_transfer_size,
                    (ops_number + resent_ops + 1) * self.ior_transfer_size
                )
            },
            "RP_3GX": {
                "engine_pool_ops_fetch": (
                    ops_number + 7,
                    ops_number + 8
                ),
                "engine_pool_ops_update": (
                    ops_number + 1,
                    ops_number + resent_ops + 2
                ),
                "engine_pool_xferred_fetch": (
                    ops_number * self.ior_transfer_size,
                    (ops_number + 1) * self.ior_transfer_size
                ),
                "engine_pool_xferred_update": (
                    3 * ops_number * self.ior_transfer_size,
                    3 * (ops_number + resent_ops + 1) * self.ior_transfer_size
                )
            }
        }
        return pool_ops_minmax[self.dfs_oclass]

    def get_metric_value(self, name, metrics_data):
        """Aggregate the metric value of the DAOS targets"""
        metric_value = 0
        metric_data = metrics_data[0]
        for host in metric_data[name]:
            for rank in metric_data[name][host]:
                for target in metric_data[name][host][rank]:
                    value_init = metric_data[name][host][rank][target]
                    value_end = metrics_data[1][name][host][rank][target]
                    self.assertLessEqual(
                        value_init, value_end,
                        "Inconsistent metric values: metric_name={}, host={}, rank={}, target={},"
                        " value_init={}, value_end={}"
                        .format(name, host, rank, target, value_init, value_end))
                    metric_value += value_end - value_init

        return metric_value

    def test_telemetry_pool_metrics(self):
        """JIRA ID: DAOS-8357

            Create a file of 500MiB thanks to ior with a transfer size of 1MiB to verify the DAOS
            engine IO telemetry basic metrics infrastructure.
        Steps:
            Create Pool
            Create Container
            Generate deterministic workload. Using ior to write 512MiB
            of data with 1MiB chunk size and 1MiB transfer size.
            Use telemetry command to get values of 4 parameters
            "engine_pool_ops_fetch", "engine_pool_ops_update",
            "engine_pool_xferred_fetch", "engine_pool_xferred_update"
            for all targets.
            Verify the sum of all parameter metrics matches the workload.
            Do this with RF=0 and RF2(but RP_3GX).
            For RF=0 the sum should be exactly equal to the expected workload
            and for RF=2 (with RP_3GX) sum should triple the size of
            workload for write but same for read.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=telemetry
        :avocado: tags=telemetry_pool_metrics,test_telemetry_pool_metrics
        """

        # create pool and container
        self.add_pool(connect=False)
        self.pool.set_property("reclaim", "disabled")
        self.add_container(pool=self.pool)

        # collect first set of pool metric data before read/write
        metric_names = [
            "engine_pool_ops_fetch",
            "engine_pool_ops_update",
            "engine_pool_xferred_fetch",
            "engine_pool_xferred_update",
            "engine_pool_resent"
        ]
        metrics_data = []
        metrics_data.append(self.telemetry.get_pool_metrics(metric_names))

        # Run ior command.
        try:
            self.update_ior_cmd_with_pool(False)
            self.ior_cmd.dfs_oclass.update(self.dfs_oclass)
            self.ior_cmd.transfer_size.update(self.ior_transfer_size)
            self.ior_cmd.dfs_chunk.update(self.ior_transfer_size)
            self.ior_cmd.block_size.update(self.ior_block_size)
            self.run_ior_with_pool(
                timeout=200, create_pool=False, create_cont=False)
        except TestFail:
            self.log.info("#ior command failed!")

        # collect second set of pool metric data after read/write
        metrics_data.append(self.telemetry.get_pool_metrics(metric_names))

        metric_values = {}
        for name in metric_names:
            metric_values[name] = self.get_metric_value(name, metrics_data)

        # perform verification check
        expected_values = self.get_expected_values(metric_values["engine_pool_resent"])
        for name in expected_values:
            val = metric_values[name]
            min_val = expected_values[name][0]
            max_val = expected_values[name][1]
            self.assertTrue(
                min_val <= val <= max_val,
                "Aggregated value of the metric {} for oclass {} is invalid: "
                "got={}, wait_in=[{}, {}]".format(name, self.dfs_oclass, val, min_val, max_val))
            self.log.debug(
                "Successfully check the metric %s for oclass %s: "
                "got=%d, wait_in=[%d, %d]", name, self.dfs_oclass, val, min_val, max_val)

        self.log.info("------Test passed------")
