"""
  (C) Copyright 2022-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from control_test_base import ControlTestBase
from exception_utils import CommandFailure


class DmgPoolQueryRanks(ControlTestBase):
    """Test dmg query ranks enabled/disabled command.

    Test Class Description:
        Simple test to verify the pool query command ranks of dmg tool.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for dmg pool query."""
        super().setUp()

        # Init the pool
        self.add_pool(connect=False)

    def test_pool_query_ranks_basic(self):
        """Test the state of ranks with dmg pool query.

        Test Description:
            Create a pool with some engines and check they are all enabled.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dmg,control,pool_query,pool_query_ranks
        :avocado: tags=DmgPoolQueryRanks,test_pool_query_ranks_basic
        """
        self.log.info("Basic tests of pool query with ranks state")

        self.log_step("Checking pool query without ranks state information")
        data = self.dmg.pool_query(self.pool.identifier)
        self._verify_ranks(None, data, "enabled_ranks")
        self._verify_ranks([], data, "disabled_ranks")

        self.log_step("Checking pool query with enabled ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
        self._verify_ranks([0, 1, 2, 3, 4], data, "enabled_ranks")
        self._verify_ranks([], data, "disabled_ranks")

        self.log_step("Checking pool query with dead ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, health_only=True)
        self._verify_ranks([], data, "dead_ranks")

    def test_pool_query_ranks_mgmt(self):
        """Test the state of ranks after excluding and reintegrate them.

        Test Description:
            Create a pool with 5 engines, first excluded engine marked as "Disabled"
            second stopped one as “Dead,” restarting it, ensuring rebuild completes,
            clearing the “Dead” status, reintegrating the excluded first engine, and
            finally verifying that all engines are enabled with the excluded rank now empty.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dmg,control,pool_query,pool_query_ranks,rebuild
        :avocado: tags=DmgPoolQueryRanks,test_pool_query_ranks_mgmt
        """
        self.log.info("Tests of pool query with ranks state when playing with ranks")

        enabled_ranks = list(range(len(self.hostlist_servers)))
        disabled_ranks = []

        all_ranks = enabled_ranks.copy()
        self.random.shuffle(all_ranks)
        exclude_rank = all_ranks[0]
        dead_rank = all_ranks[1]
        self.log_step(f"Excluding pool rank:{exclude_rank} all_ranks={all_ranks}")
        self.pool.exclude([exclude_rank])
        enabled_ranks.remove(exclude_rank)
        disabled_ranks = sorted(disabled_ranks + [exclude_rank])

        self.log_step("Checking enabled ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
        self._verify_ranks(enabled_ranks, data, "enabled_ranks")
        self._verify_ranks(disabled_ranks, data, "disabled_ranks")

        self.log_step(f"Waiting for rebuild to start after excluding pool rank {exclude_rank}")
        self.pool.wait_for_rebuild_to_start()

        # kill second rank.
        self.log_step(f"Stopping rank:{dead_rank} all_ranks={all_ranks}")
        self.server_managers[0].stop_ranks([dead_rank])

        self.log_step(f"Waiting for pool rank {dead_rank} to be dead")
        self.pool.wait_pool_dead_ranks([dead_rank], timeout=30)
        self._verify_ranks(disabled_ranks, data, "disabled_ranks")

        self.log_step(f"Starting rank {dead_rank}")
        self.server_managers[0].start_ranks([dead_rank])

        self.log_step("Waiting for pool ranks to no longer be dead")
        self.pool.wait_pool_dead_ranks([], timeout=30)

        self.log_step("Waiting for rebuild to complete")
        self.pool.wait_for_rebuild_to_end()

        self.log_step(f"Reintegrating rank {exclude_rank}")
        cmd_succeed = False
        for _ in range(3):
            try:
                self.pool.reintegrate(exclude_rank)
                cmd_succeed = True
                break
            except CommandFailure:
                self.log.debug("dmg command failed retry")
            time.sleep(3)

        self.assertTrue(cmd_succeed, "pool reintegrate failed")
        self.log_step(f"Waiting for rebuild to complete after reintegrating rank {exclude_rank}")
        self.pool.wait_for_rebuild_to_start()
        self.pool.wait_for_rebuild_to_end()

        enabled_ranks = sorted(enabled_ranks + [exclude_rank])
        disabled_ranks.remove(exclude_rank)

        self.log_step("Checking enabled ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
        self._verify_ranks(enabled_ranks, data, "enabled_ranks")
        self._verify_ranks(disabled_ranks, data, "disabled_ranks")

    def _verify_ranks(self, expect, data, key):
        """Verify the expected and actual rank lists are equal.

        Args:
            expect (list): list of ranks to expect
            data (dict): dmg json response containing actual list of ranks
            key (str): the dmg json response key used to access the actual list of ranks
        """
        actual = data["response"].get(key)
        if expect is None:
            self.assertIsNone(actual, f"Invalid {key} field: want=None, got={actual}")
        else:
            self.assertListEqual(
                actual, expect, f"Invalid {key} field: want={expect}, got={actual}")
        self.log.debug("Check of %s passed: %s == %s", key, expect, actual)
