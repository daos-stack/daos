"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from ior_test_base import IorTestBase

# Pretty print constant
RESULT_OK = "[\033[32m OK\033[0m]"
RESULT_NOK = "[\033[31mNOK\033[0m]"

TIMEOUT_DEADLINE = 60


class PoolTargetQueryScmTest(IorTestBase):
    """Test SCM target pool query

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a TelemetryPoolSpaceMetrics object."""
        super().__init__(*args, **kwargs)

        self.server_count = 0
        self.target_count = 0
        self.scm_metadata_interval = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        engine_count = self.server_managers[0].get_config_value("engines_per_host")
        self.server_count = len(self.hostlist_servers) * engine_count
        self.target_count = self.server_managers[0].get_config_value("targets")
        self.scm_metadata_interval = self.params.get(
            "metadata_max_size", "/run/scm_thresholds/*")

    def get_scm_used(self):
        """Get the SCM space used from all targets.

        return:
            int: scm space used from all targets.
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

    def test_pool_target_query_scm(self):
        """JIRA ID: DAOS-12968

        Check if the SCM space used return by the target_query() function is consistent.

        Steps:
            Create a pool and a container
            Check the SCM space used
            Generate deterministic workload.  Using ior to write 128MiB of data.
            Check the SCM space used

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=PoolTargetQueryScmTest,test_pool_target_query_scm
        """
        test_failed_nb = 0

        # create pool and container
        self.add_pool(connect=False)
        self.pool.set_property("reclaim", "disabled")
        self.add_container(pool=self.pool)

        # perform SCM usage verification check
        timeout = TIMEOUT_DEADLINE
        while timeout > 0:
            scm_used = self.get_scm_used()
            min_val, max_val = self.scm_metadata_interval[0]
            if min_val <= scm_used <= max_val:
                self.log.info(
                    "Successfully check the SCM used: got=%d, wait_in=[%d, %d], timeout=%d",
                    scm_used, min_val, max_val, timeout)
                break
            test_failed_nb += 1
            self.log.error(
                "Aggregated value of the SCM used is invalid: got=%d, wait_in=[%d, %d], timeout=%d",
                scm_used, min_val, max_val, timeout)
            time.sleep(1)
            timeout -= 1

        # Run ior command.
        self.update_ior_cmd_with_pool(False)
        self.run_ior_with_pool(
            timeout=200, create_pool=False, create_cont=False)

        # perform SCM usage verification check
        old_val = scm_used
        scm_used = self.get_scm_used()
        min_val, max_val = self.scm_metadata_interval[1]
        if min_val <= scm_used <= max_val:
            self.log.info(
                "Successfully check the SCM used: got=%d, wait_in=[%d, %d]",
                scm_used, min_val, max_val)
        else:
            test_failed_nb += 1
            self.log.error(
                "Aggregated value of the SCM used is invalid: got=%d, wait_in=[%d, %d]",
                scm_used, min_val, max_val)

        if old_val < scm_used:
            self.log.info(
                "Successfully check the SCM used: got=%d, wait>%d",
                scm_used, old_val)
        else:
            test_failed_nb += 1
            self.log.error(
                "Aggregated value of the SCM used is invalid: got=%d, wait>%d",
                scm_used, old_val)

        self.log.info("\n")
        self.log.info("############ Test Results ############")
        self.log.info(
            "# Initial value of SCM usage:\t%s",
            RESULT_OK if timeout == TIMEOUT_DEADLINE else RESULT_NOK)
        self.log.info(
            "# Final value of the SCM usage:\t%s",
            RESULT_OK if min_val <= scm_used <= max_val and old_val < scm_used else RESULT_NOK)
        self.log.info("######################################")
        self.assertTrue(
            test_failed_nb == 0,
            "{} SCM target query tests have failed".format(test_failed_nb))
