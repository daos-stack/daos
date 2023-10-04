"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase
from telemetry_test_base import TestWithTelemetry


class TelemetryPoolMetrics(IorTestBase, TestWithTelemetry):
    # pylint: disable=too-many-nested-blocks
    """Test telemetry pool basic metrics.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TelemetryPoolMetrics object."""
        super().__init__(*args, **kwargs)

        self.dfs_oclass = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        self.dfs_oclass = self.params.get("oclass", '/run/container/*', self.dfs_oclass)

    def get_expected_value_range(self):
        """Return the expected metrics value output.

        This function returns a hash map of pairs defining min and max values of each tested
        telemetry metrics.

        Returns:
            dict: Dictionary of the expected metrics value output.
        """

        ops_number = int(self.ior_cmd.block_size.value / self.ior_cmd.transfer_size.value)
        pool_ops_minmax = {
            "SX": {
                "engine_pool_ops_fetch": (
                    ops_number + 7,
                    ops_number + 8
                ),
                "engine_pool_ops_update": (
                    ops_number + 1,
                    ops_number + 2
                ),
                "engine_pool_xferred_fetch": (
                    ops_number * self.ior_cmd.transfer_size.value,
                    (ops_number + 1) * self.ior_cmd.transfer_size.value
                ),
                "engine_pool_xferred_update": (
                    ops_number * self.ior_cmd.transfer_size.value,
                    (ops_number + 1) * self.ior_cmd.transfer_size.value
                )
            },
            "RP_3GX": {
                "engine_pool_ops_fetch": (
                    ops_number + 7,
                    ops_number + 8
                ),
                "engine_pool_ops_update": (
                    ops_number + 1,
                    ops_number + 2
                ),
                "engine_pool_xferred_fetch": (
                    ops_number * self.ior_cmd.transfer_size.value,
                    (ops_number + 1) * self.ior_cmd.transfer_size.value
                ),
                "engine_pool_xferred_update": (
                    3 * ops_number * self.ior_cmd.transfer_size.value,
                    3 * (ops_number + 1) * self.ior_cmd.transfer_size.value
                )
            }
        }
        return pool_ops_minmax[self.dfs_oclass]

    def get_metrics(self, names):
        """Obtain the specified metrics information.

        Args:
            name (list): List of metric names to query.

        Returns:
            dict: a dictionary of metric keys linked to their aggregated values.
        """
        metrics = {}
        for name in names:
            metrics[name] = 0

        for data in self.telemetry.get_metrics(",".join(names)).values():
            for name, value in data.items():
                for metric in value["metrics"]:
                    metrics[name] += metric["value"]

        return metrics

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
        :avocado: tags=TelemetryPoolMetrics,test_telemetry_pool_metrics
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
            "engine_net_req_timeout",
            "engine_pool_resent"
        ]
        metrics_init = self.get_metrics(metric_names)

        # Run ior command.
        self.update_ior_cmd_with_pool(False)
        self.ior_cmd.dfs_oclass.update(self.dfs_oclass)
        self.ior_cmd.dfs_chunk.update(self.ior_cmd.transfer_size.value)
        # NOTE DAOS-12946: Not catching ior failures is intended.  Indeed, to properly test the
        # metrics we have to exactly know how much data have been transferred.
        self.run_ior_with_pool(timeout=200, create_pool=False, create_cont=False)

        # collect second set of pool metric data after read/write
        metrics_end = self.get_metrics(metric_names)

        # Compute the number of operations done
        metrics = {}
        for name in metric_names:
            metrics[name] = metrics_end[name] - metrics_init[name]
            self.log.debug(
                "Successfully retrieve metric: %s=%d (init=%d, end=%d)",
                name, metrics[name], metrics_init[name], metrics_end[name])

        # NOTE DAOS-14220: Check if networking errors occurred during the test.  If yes, we skip the
        # test as there is no reliable way to know how many fetch and update operations were
        # effectively performed.
        timeout_ops = metrics["engine_net_req_timeout"]
        resent_ops = metrics["engine_pool_resent"]
        if timeout_ops > 0 or resent_ops > 0:
            self.log.info(
                "Transient networking errors occurred during the test: "
                "timeout_ops=%d, resent_ops=%d",
                timeout_ops, resent_ops)
            self.log.info("------Test skipped------")
            return

        # perform verification check
        expected_values = self.get_expected_value_range()
        for name in expected_values:
            val = metrics[name]
            min_val, max_val = expected_values[name]
            self.assertTrue(
                min_val <= val <= max_val,
                "Aggregated value of the metric {} for oclass {} is invalid: "
                "got={}, wait_in=[{}, {}]".format(name, self.dfs_oclass, val, min_val, max_val))
            self.log.debug(
                "Successfully check the metric %s for oclass %s: "
                "got=%d, wait_in=[%d, %d]", name, self.dfs_oclass, val, min_val, max_val)

        self.log.info("------Test passed------")

    def test_telemetry_pool_metrics_sanity_check(self):
        """JIRA ID: DAOS-13146

            Create a pool and check whether all the pool metrics listed
            in the ENGINE_POOL_METRICS are valid.
        Steps:
            Create Pool
            Get all the pool metrics and check for any errors.
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=telemetry
        :avocado: tags=TelemetryPoolMetrics,test_telemetry_pool_metrics_sanity_check
        """
        # Create a Pool
        self.add_pool(connect=False)
        # Get all the default Pool Metrics and check for any errors.
        # If errors are noticed, get_pool_metrics will report them and
        # fail the test.
        self.telemetry.get_pool_metrics()
        self.log.info("Test Passed")
