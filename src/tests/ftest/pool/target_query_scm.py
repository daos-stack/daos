"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase


class PoolTargetQueryScmTest(IorTestBase):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
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

    def check_scm_used(self, scm_used, wait_range):
        """Check if a SCM usage value is in an expected range.

        Args:
            scm_used (int): The value to check.
            wait_range (list): Min and max expected values.
        """
        min_val, max_val = wait_range
        self.assertTrue(
            min_val <= scm_used <= max_val,
            "Aggregated value of the SCM used is invalid: got={}, wait_in=[{}, {}]"
            .format(scm_used, min_val, max_val))
        self.log.info(
            "Successfully check the SCM used: got=%d, wait_in=[%d, %d]",
            scm_used, min_val, max_val)

    def test_pool_target_query_scm(self):
        """JIRA ID: DAOS-XXXXX

        TODO

        Steps:
            TODO

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=PoolTargetQueryScmTest,test_pool_target_query_scm
        """

        # create pool and container
        self.add_pool(connect=False)
        self.pool.set_property("reclaim", "disabled")
        self.add_container(pool=self.pool)

        # perform SCM usage verification check
        got_value = self.get_scm_used()
        wait_range = self.scm_metadata_interval[0]
        self.check_scm_used(got_value, wait_range)

        # Run ior command.
        self.update_ior_cmd_with_pool(False)
        self.run_ior_with_pool(
            timeout=200, create_pool=False, create_cont=False)

        # perform SCM usage verification check
        old_value = got_value
        got_value = self.get_scm_used()
        wait_range = self.scm_metadata_interval[1]
        self.check_scm_used(got_value, wait_range)
        self.assertTrue(
            old_value < got_value,
            "Aggregated value of the SCM used is invalid: got={}, wait>{}"
            .format(got_value, old_value))
        self.log.info(
            "Successfully check the SCM used: got=%d, wait>%d",
            got_value, old_value)

        self.log.info("------Test passed------")
