"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from ior_test_base import IorTestBase
from telemetry_test_base import TestWithTelemetry

# Pretty print constant
RESULT_OK = "[\033[32m OK\033[0m]"
RESULT_NOK = "[\033[31mNOK\033[0m]"

# It should take as much as 10s for the vos space metrics to be up to date: it is equal to the idle
# GC ULT interval time.
TIMEOUT_DEADLINE = 15


class TelemetryPoolSpaceMetrics(IorTestBase, TestWithTelemetry):
    """Test telemetry pool space basic metrics.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TelemetryPoolSpaceMetrics object."""
        super().__init__(*args, **kwargs)

        self.metric_names = [
            'engine_pool_vos_space_scm_used',
            'engine_pool_vos_space_nvme_used']
        self.data_size = 0
        self.scm_data_size_percent = None
        self.scm_metadata_max_size = 0
        self.pool_space_metrics_minmax = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        self.data_size = self.ior_cmd.block_size.value
        self.scm_metadata_max_size = self.params.get(
            "metadata_max_size", "/run/scm_metric_thresholds/*")
        self.pool_space_metrics_minmax = {
            "/run/pool_scm/*": {
                "engine_pool_vos_space_scm_used": (
                    self.data_size,
                    self.data_size + self.scm_metadata_max_size
                ),
                "engine_pool_vos_space_nvme_used": (0, 0)
            },
            "/run/pool_scm_nvme/*": {
                "engine_pool_vos_space_scm_used": (1, self.scm_metadata_max_size),
                "engine_pool_vos_space_nvme_used": (self.data_size, self.data_size)
            }
        }

    def get_expected_values_range(self, namespace):
        """Return the expected metrics value output.

        This function returns a hash map of pairs defining min and max values of each tested
        telemetry metrics.  The hash map of pairs returned depends on the pool created with the
        given namespace and the size of the data written in it.

        Args:
            namespace (string): Namespace of the last created pool.

        Returns:
            expected_values (dict): Dictionary of the expected metrics value output.
        """

        self.assertIn(
            namespace, self.pool_space_metrics_minmax,
            "Invalid pool namespace: {}".format(namespace))

        return self.pool_space_metrics_minmax[namespace]

    def get_metrics(self, names):
        """Obtain the specified metrics information.

        Args:
            names (list): List of metric names to query.

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

    def test_telemetry_pool_space_metrics(self):
        """JIRA ID: DAOS-10192

        Create a file of 16MiB thanks to ior to verify the DAOS engine IO telemetry vos space
        metrics.

        Steps:
            Create a pool
            Create a container
            Generate deterministic workload.  Using ior to write 128MiB of data.
            Use telemetry command to get value of vos space metrics "engine_pool_vos_space_scm_used"
            and "engine_pool_vos_space_nvme_used" for all targets.
            Verify the sum of all parameter metrics matches the workload.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=telemetry
        :avocado: tags=TelemetryPoolSpaceMetrics,test_telemetry_pool_space_metrics
        """

        test_timeouts = {}
        for namespace in ["/run/pool_scm/*", "/run/pool_scm_nvme/*"]:
            test_name = namespace.split("/")[2]
            self.log.debug("Starting test %s", test_name)

            # create pool and container
            self.add_pool(namespace=namespace, create=True, connect=False)
            self.pool.disable_aggregation()
            self.add_container(pool=self.pool)

            # Run ior command.
            self.update_ior_cmd_with_pool(create_cont=False)
            self.run_ior_with_pool(
                timeout=200, create_pool=False, create_cont=False)

            # Testing VOS space metrics
            expected_values = self.get_expected_values_range(namespace)
            test_counter = 0
            timeout = TIMEOUT_DEADLINE
            while timeout > 0:
                metrics = self.get_metrics(self.metric_names)

                is_metric_ok = True
                for name in self.metric_names:
                    val = metrics[name]
                    min_val, max_val = expected_values[name]
                    is_metric_ok &= min_val <= val <= max_val
                    self.log.debug(
                        "Check of the metric %s: got=%d, wait_in=[%d, %d], ok=%r, timeout=%d",
                        name, val, min_val, max_val, is_metric_ok, timeout)
                test_counter = (test_counter + 1) if is_metric_ok else 0

                if test_counter >= 2:
                    self.log.info(
                        "Test %s successfully completed in %d sec",
                        test_name, TIMEOUT_DEADLINE - timeout)
                    break

                time.sleep(1)
                timeout -= 1

            test_timeouts[test_name] = timeout

            self.destroy_containers(self.container)
            self.destroy_pools(self.pool)

        self.log.info("\n############ Test Results ############")
        for test_name, timeout in test_timeouts.items():
            self.log.info(
                "# Test %s:\t%s",
                test_name, RESULT_OK if timeout > 0 else RESULT_NOK)
        self.log.info("######################################")
        self.assertTrue(
            0 not in test_timeouts.values(),
            "One or more vos space metric tests have failed")
