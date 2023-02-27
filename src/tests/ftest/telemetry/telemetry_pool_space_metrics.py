"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail
from ior_test_base import IorTestBase
from telemetry_test_base import TestWithTelemetry


class TelemetryPoolSpaceMetrics(IorTestBase, TestWithTelemetry):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Test telemetry pool space basic metrics.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TelemetryPoolSpaceMetrics object."""
        super().__init__(*args, **kwargs)

        self.server_count = 0
        self.target_count = 0

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        engine_count = self.server_managers[0].get_config_value("engines_per_host")
        self.server_count = len(self.hostlist_servers) * engine_count
        self.target_count = self.server_managers[0].get_config_value("targets")

    def get_scm_used(self):
        """Get the SCM space used from all targets.

        return:
            scm_used (int): scm space used from all targets.
        """
        self.pool.connect()

        # Collect the space information for all the ranks and targets
        scm_used = 0
        for rank_idx in range(self.server_count):
            for target_idx in range(self.target_count):
                result = self.pool.pool.target_query(target_idx, rank_idx)
                if result.ta_space.s_total[0] == result.ta_space.s_free[0]:
                    return -1
                scm_used += result.ta_space.s_total[0] - result.ta_space.s_free[0]

        return scm_used

    def get_metrics(self, names):
        """Obtain the specified metrics information.

        Args:
            name (list): List of metric names to query.

        Returns:
            metrics (dict): a dictionary of metric keys linked to their aggregated values.
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
            Generate deterministic workload.  Using ior to write 16MiB of data.
            Use telemetry command to get value of vos space metrics "engine_pool_vos_space_scm_used"
            and "engine_pool_vos_space_nvme_used" for all targets.
            Verify the sum of all parameter metrics matches the workload.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=telemetry
        :avocado: tags=telemetry_pool_space_metrics,test_telemetry_pool_space_metrics
        """

        # create pool and container
        self.add_pool(connect=False)
        self.pool.set_property("reclaim", "disabled")
        self.add_container(pool=self.pool)

        # Run ior command.
        try:
            self.update_ior_cmd_with_pool(False)
            self.run_ior_with_pool(
                timeout=200, create_pool=False, create_cont=False)
        except TestFail as error:
            self.log.info("ior command failed: %s", str(error))
            raise

        # collect pool space metric data after write
        metric_names = [
            'engine_pool_vos_space_scm_used',
            'engine_pool_vos_space_nvme_used']
        metrics = self.get_metrics(metric_names)

        # perform SCM usage verification check
        wait_value = self.get_scm_used()
        if wait_value == -1:
            self.log.warning(
                "Insane storage usage value(s) returned with PMDK: "
                "Not able to test the metric engine_pool_vos_space_scm_used")
        else:
            got_value = metrics["engine_pool_vos_space_scm_used"]
            self.assertEqual(
                got_value,
                wait_value,
                "Aggregated value of the metric engine_pool_vos_space_scm_used "
                "is invalid: got={}, wait={}".format(got_value, wait_value))
            self.log.debug("Successfully check the metric engine_pool_vos_space_scm_used")

        # perform NVME usage verification check
        got_value = metrics["engine_pool_vos_space_nvme_used"]
        wait_value = self.ior_cmd.block_size.value
        self.assertEqual(
            got_value,
            wait_value,
            "Aggregated value of the metric engine_pool_vos_space_nvme_used "
            "is invalid: got={}, wait={}".format(got_value, wait_value))
        self.log.debug("Successfully check the metric engine_pool_vos_space_nvme_used")

        self.log.info("------Test passed------")
