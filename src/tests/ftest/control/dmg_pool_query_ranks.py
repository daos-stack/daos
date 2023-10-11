"""
  (C) Copyright 2022-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from exception_utils import CommandFailure
from control_test_base import ControlTestBase


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
            data['response']['enabled_ranks'],
            "Invalid enabled_ranks field: want=None, got={}".format(
                data['response']['enabled_ranks']))
        self.assertIsNone(
            data['response']['disabled_ranks'],
            "Invalid disabled_ranks field: want=None, got={}".format(
                data['response']['disabled_ranks']))

        self.log.debug("Checking enabled ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
        self.assertListEqual(
            data['response']['enabled_ranks'], [0, 1, 2],
            "Invalid enabled_ranks field: want=[0, 1, 2], got={}".format(
                data['response']['enabled_ranks']))
        self.assertIsNone(
            data['response']['disabled_ranks'],
            "Invalid disabled_ranks field: want=None, got={}".format(
                data['response']['disabled_ranks']))

        self.log.debug("Checking disabled ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, show_disabled=True)
        self.assertIsNone(
            data['response']['enabled_ranks'],
            "Invalid enabled_ranks field: want=None, got={}".format(
                data['response']['enabled_ranks']))
        self.assertListEqual(
            data['response']['disabled_ranks'], [],
            "Invalid disabled_ranks field: want=[], got={}".format(
                data['response']['disabled_ranks']))

    def test_pool_query_ranks_error(self):
        """Test that ranks state option are mutually exclusive.

        Test Description:
            Check that options '--show-enabled' and '--show-disabled" are mutually exclusive.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dmg,control,pool_query,pool_query_ranks
        :avocado: tags=DmgPoolQueryRanks,test_pool_query_ranks_error
        """
        self.log.info("Tests of pool query with incompatible options")

        # Disable raising an exception if the dmg command fails
        self.dmg.exit_status_exception = False
        try:
            data = self.dmg.pool_query(self.pool.identifier, show_enabled=True, show_disabled=True)
            self.assertIsNotNone(data["error"], "Expected error not returned")
            self.assertIn(r'may not be mixed with', str(data['error']), "Invalid error message")
        finally:
            self.dmg.exit_status_exception = True

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
        self.log.info("Starting excluding ranks: all_ranks=%s", all_ranks)
        for rank in all_ranks:
            self.log.debug("Excluding rank %d", rank)
            self.pool.exclude([rank])
            enabled_ranks.remove(rank)
            disabled_ranks = sorted(disabled_ranks + [rank])

            self.log.debug("Checking enabled ranks state information")
            data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
            self.assertListEqual(
                data['response']['enabled_ranks'], enabled_ranks,
                "Invalid enabled_ranks field: want={}, got={}".format(
                    enabled_ranks, data['response']['enabled_ranks']))

            self.log.debug("Checking disabled ranks state information")
            data = self.dmg.pool_query(self.pool.identifier, show_disabled=True)
            self.assertListEqual(
                data['response']['disabled_ranks'], disabled_ranks,
                "Invalid disabled_ranks field: want={}, got={}".format(
                    disabled_ranks, data['response']['disabled_ranks']))

            self.log.debug("Waiting for pool to be rebuild")
            self.pool.wait_for_rebuild_to_start()
            self.pool.wait_for_rebuild_to_end()

        self.random.shuffle(all_ranks)
        self.log.info("Starting reintegrating ranks: all_ranks=%s", all_ranks)
        for rank in all_ranks:
            self.log.debug("Reintegrating rank %d", rank)

            cmd_succeed = False
            for _ in range(3):
                try:
                    result = self.pool.reintegrate(rank)
                    cmd_succeed = True
                    break
                except CommandFailure:
                    self.log.debug("dmg command failed retry")
                time.sleep(3)

            self.assertTrue(cmd_succeed, "pool reintegrate failed: {}".format(result))
            enabled_ranks = sorted(enabled_ranks + [rank])
            disabled_ranks.remove(rank)

            self.log.debug("Checking enabled ranks state information")
            data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
            self.assertListEqual(
                data['response']['enabled_ranks'], enabled_ranks,
                "Invalid enabled_ranks field: want={}, got={}".format(
                    enabled_ranks, data['response']['enabled_ranks']))

            self.log.debug("Checking disabled ranks state information")
            data = self.dmg.pool_query(self.pool.identifier, show_disabled=True)
            self.assertListEqual(
                data['response']['disabled_ranks'], disabled_ranks,
                "Invalid disabled_ranks field: want={}, got={}".format(
                    disabled_ranks, data['response']['disabled_ranks']))

            self.log.debug("Waiting for pool to be rebuild")
            self.pool.wait_for_rebuild_to_start()
            self.pool.wait_for_rebuild_to_end()
