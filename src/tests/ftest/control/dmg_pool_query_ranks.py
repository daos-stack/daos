"""
  (C) Copyright 2022-2024 Intel Corporation.

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

        self.log.debug("Checking without ranks state information")
        data = self.dmg.pool_query(self.pool.identifier)
        self.assertIsNone(
            data['response'].get('enabled_ranks'),
            "Invalid enabled_ranks field: want=None, got={}".format(
                data['response'].get('enabled_ranks')))
        self.assertListEqual(
            data['response'].get('disabled_ranks'), [],
            "Invalid disabled_ranks field: want=[], got={}".format(
                data['response'].get('disabled_ranks')))

        self.log.debug("Checking enabled ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
        self.assertListEqual(
            data['response'].get('enabled_ranks'), [0, 1, 2, 3, 4],
            "Invalid enabled_ranks field: want=[0, 1, 2, 3, 4], got={}".format(
                data['response'].get('enabled_ranks')))
        self.assertListEqual(
            data['response'].get('disabled_ranks'), [],
            "Invalid suspect_ranks field: want=[], got={}".format(
                data['response'].get('disabled_ranks')))

        self.log.debug("Checking suspect ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, health_only=True)
        self.assertListEqual(
            data['response'].get('suspect_ranks'), [],
            "Invalid suspect_ranks field: want=[], got={}".format(
                data['response'].get('suspect_ranks')))

    def test_pool_query_ranks_mgmt(self):
        """Test the state of ranks after excluding and reintegrate them.

        Test Description:
            Create a pool with some engines exclude them one by one and check the consistency of the
            list of enabled and disabled ranks.  Then, reintegrate them and check the consistency of
            the list of enabled and disabled ranks.

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
        suspect_rank = all_ranks[1]
        self.log.info("Starting excluding rank:%d all_ranks=%s", exclude_rank, all_ranks)
        self.pool.exclude([exclude_rank])
        enabled_ranks.remove(exclude_rank)
        disabled_ranks = sorted(disabled_ranks + [exclude_rank])

        self.log.debug("Checking enabled ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
        self.assertListEqual(
            data['response'].get('enabled_ranks'), enabled_ranks,
            "Invalid enabled_ranks field: want={}, got={}".format(
                enabled_ranks, data['response'].get('enabled_ranks')))
        self.assertListEqual(
            data['response'].get('disabled_ranks'), disabled_ranks,
            "Invalid disabled_ranks field: want={}, got={}".format(
                disabled_ranks, data['response'].get('disabled_ranks')))

        self.log.debug("Waiting for pool to be rebuild")
        self.pool.wait_for_rebuild_to_start()

        # kill second rank.
        self.server_managers[0].stop_ranks([suspect_rank], self.d_log, force=True)
        self.pool.wait_pool_suspect_ranks([suspect_rank], timeout=30)
        self.assertListEqual(
            data['response'].get('disabled_ranks'), disabled_ranks,
            "Invalid disabled_ranks field: want={}, got={}".format(
                disabled_ranks, data['response'].get('disabled_ranks')))

        self.server_managers[0].start_ranks([suspect_rank], self.d_log)
        self.pool.wait_pool_suspect_ranks([], timeout=30)
        self.pool.wait_for_rebuild_to_end()

        self.log.debug("Reintegrating rank %d", exclude_rank)
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
        self.log.debug("Waiting for pool to be rebuild")
        self.pool.wait_for_rebuild_to_start()
        # Fix this after DAOS-16702
        # self.pool.wait_for_rebuild_to_end

        enabled_ranks = sorted(enabled_ranks + [exclude_rank])
        disabled_ranks.remove(exclude_rank)

        self.log.debug("Checking enabled ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
        self.assertListEqual(
            data['response'].get('enabled_ranks'), enabled_ranks,
            "Invalid enabled_ranks field: want={}, got={}".format(
                enabled_ranks, data['response'].get('enabled_ranks')))
        self.assertListEqual(
            data['response'].get('disabled_ranks'), disabled_ranks,
            "Invalid disabled_ranks field: want={}, got={}".format(
                disabled_ranks, data['response'].get('disabled_ranks')))
