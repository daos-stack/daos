"""
  (C) Copyright 2018-2023 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

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

    def __get_metrics(self, names):
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
        :avocado: tags=telemetry,pool
        :avocado: tags=TelemetryPoolSpaceMetrics,test_telemetry_pool_space_metrics
        """
        metric_names = [
            'engine_pool_vos_space_scm_used',
            'engine_pool_vos_space_nvme_used']

        data_size = self.ior_cmd.block_size.value
        scm_metadata_max_size = self.params.get(
            "metadata_max_size", "/run/scm_metric_thresholds/*")
        pool_space_metrics_minmax = {
            "/run/pool_scm/*": {
                "engine_pool_vos_space_scm_used": (
                    data_size,
                    data_size + scm_metadata_max_size
                ),
                "engine_pool_vos_space_nvme_used": (0, 0)
            },
            "/run/pool_scm_nvme/*": {
                "engine_pool_vos_space_scm_used": (1, scm_metadata_max_size),
                "engine_pool_vos_space_nvme_used": (data_size, data_size)
            }
        }

        test_timeouts = {}
        for namespace in ["/run/pool_scm/*", "/run/pool_scm_nvme/*"]:
            if namespace not in pool_space_metrics_minmax:
                self.fail(f"Invalid pool namespace: {namespace}")
            expected_values = pool_space_metrics_minmax[namespace]
            test_name = namespace.split("/")[2]

            self.log_step(f"Starting test {test_name} with pool namespace {namespace}")
            self.pool = self.get_pool(namespace=namespace, connect=False)
            self.pool.disable_aggregation()
            self.container = self.get_container(pool=self.pool)

            self.log_step("Run IOR to write data to the container")
            self.update_ior_cmd_with_pool(create_cont=False)
            self.run_ior_with_pool(timeout=200, create_pool=False, create_cont=False)

            self.log_step("Verify the metrics values")
            test_counter = 0
            timeout = TIMEOUT_DEADLINE
            while timeout > 0:
                metrics = self.__get_metrics(metric_names)

                is_metric_ok = True
                for name in metric_names:
                    if name not in expected_values:
                        self.fail(f"Invalid metric name: {name}")
                    val = metrics[name]
                    min_val, max_val = expected_values[name]
                    is_this_metric_ok = min_val <= val <= max_val
                    is_metric_ok &= is_this_metric_ok
                    self.log.debug(
                        "Check of the metric %s: got=%d, wait_in=[%d, %d], ok=%r, timeout=%d",
                        name, val, min_val, max_val, is_this_metric_ok, timeout)
                test_counter = (test_counter + 1) if is_metric_ok else 0

                if test_counter >= 2:
                    self.log_step(
                        f"Test {test_name} successfully completed "
                        f"in {TIMEOUT_DEADLINE - timeout} sec")
                    break

                time.sleep(1)
                timeout -= 1

            test_timeouts[test_name] = timeout

            self.container.destroy()
            self.pool.destroy()

        self.log.info("\n############ Test Results ############")
        for test_name, timeout in test_timeouts.items():
            self.log.info(
                "# Test %s:\t%s",
                test_name, RESULT_OK if timeout > 0 else RESULT_NOK)
        self.log.info("######################################")
        self.assertTrue(
            0 not in test_timeouts.values(),
            "One or more vos space metric tests have failed")
