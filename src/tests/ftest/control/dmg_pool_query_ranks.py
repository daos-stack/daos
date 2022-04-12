#!/usr/bin/python3
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from control_test_base import ControlTestBase


class DmgPoolQueryRanks(ControlTestBase):
    # pylint: disable=too-many-ancestors
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
        """Test the state of ranks with dmg pool query

        Test Description:
            Create a pool with some engines and check they are all enabled.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dmg,pool_query,pool_query_ranks
        :avocado: tags=pool_query_ranks_basic
        """
        self.log.info("Basic tests of pool query with ranks state")

        self.log.debug("Checking without ranks state information")
        data = self.dmg.pool_query(self.pool.identifier)
        self.assertIsNone(data['response']['enabled_ranks'],
                "Invalid enabled_ranks field: want=None, "
                "got={}".format(data['response']['enabled_ranks']))
        self.assertIsNone(data['response']['disabled_ranks'],
                "Invalid disabled_ranks field: want=None, "
                "got={}".format(data['response']['disabled_ranks']))

        self.log.debug("Checking enabled ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
        self.assertListEqual(data['response']['enabled_ranks'], [0, 1, 2],
                "Invalid enabled_ranks field: want=None, "
                "got={}".format(data['response']['enabled_ranks']))
        self.assertIsNone(data['response']['disabled_ranks'],
                "Invalid disabled_ranks field: want=None, "
                "got={}".format(data['response']['disabled_ranks']))

        self.log.debug("Checking disabled ranks state information")
        data = self.dmg.pool_query(self.pool.identifier, show_disabled=True)
        self.assertIsNone(data['response']['enabled_ranks'],
                "Invalid enabled_ranks field: want=None, "
                "got={}".format(data['response']['enabled_ranks']))
        self.assertListEqual(data['response']['disabled_ranks'], [],
                "Invalid disabled_ranks field: want=None, "
                "got={}".format(data['response']['disabled_ranks']))

    def test_pool_query_ranks_error(self):
        """Test that ranks state option are mutually exclusive

        Test Description:
            Check that options '--show-enabled' and '--show-disabled" are muttually exclusive.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dmg,pool_query,pool_query_ranks
        :avocado: tags=pool_query_ranks_error
        """
        self.log.info("Tests of pool query with incompatible options")

        # Disable raising an exception if the dmg command fails
        self.dmg.exit_status_exception = False
        try:
            data = self.dmg.pool_query(self.pool.identifier, show_enabled=True, show_disabled=True)
            self.assertIsNotNone(data["error"], "Invalid error field: want={}, "
                    "got=None".format(data['error']))
            self.assertIn(r'may not be mixed with', str(data['error']), "Invalid error message")
        finally:
            self.dmg.exit_status_exception = True

    def test_pool_query_ranks_mgmt(self):
        """Test the state of ranks after excluding and reintegrate them

        Test Description:
            Create a pool with some engines exclude them one by one and check the consistency of the
            list of enabled and disabled ranks.  Then, reintegrate them and check the consistency of
            the list of enabled and disabled ranks.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=dmg,pool_query,pool_query_ranks
        :avocado: tags=pool_query_ranks_mgmt
        """
        self.log.info("Tests of pool query with ranks state when playing with ranks")

        enabled_ranks = [0, 1, 2]
        disabled_ranks = []
        for rank in [1, 0, 2]:
            self.log.debug("Excluding rank %d", rank)
            self.dmg.pool_exclude(self.pool.uuid, rank)
            enabled_ranks.remove(rank)
            disabled_ranks = sorted(disabled_ranks + [rank])

            self.log.debug("Checking enabled ranks state information")
            data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
            self.assertListEqual(data['response']['enabled_ranks'], enabled_ranks,
                    "Invalid enabled_ranks field: want={}, "
                    "got={}".format(enabled_ranks, data['response']['enabled_ranks']))

            self.log.debug("Checking disabled ranks state information")
            data = self.dmg.pool_query(self.pool.identifier, show_disabled=True)
            self.assertListEqual(data['response']['disabled_ranks'], disabled_ranks,
                    "Invalid disabled_ranks field: want={}, "
                    "got={}".format(disabled_ranks, data['response']['disabled_ranks']))

        for rank in [2, 0, 1]:
            self.log.debug("Reintegrating rank %d", rank)
            self.dmg.exit_status_exception = False
            # NOTE Reintegrating ranks could not be done immediately => looping with timeout until
            # the eviction is done.
            timeout = 60
            try:
                while timeout > 0:
                    rc = self.dmg.pool_reintegrate(self.pool.uuid, rank)
                    if rc.exit_status == 0:
                        break
                    self.assertIn('DER_BUSY', str(rc.stdout),
                            "Pool reintegration failed: {}".format(str(rc.stdout)))
                    self.log.debug('Pool reintegration of rank=%d failed: msg="Resource busy", '
                            'timeout=%d', rank, timeout)
                    time.sleep(1.)
                    timeout -= 1
            finally:
                self.dmg.exit_status_exception = True
            self.assertNotEqual(timeout, 0, "Rank {} could not be reintegrated".format(rank))

            enabled_ranks = sorted(enabled_ranks + [rank])
            disabled_ranks.remove(rank)

            self.log.debug("Checking enabled ranks state information")
            data = self.dmg.pool_query(self.pool.identifier, show_enabled=True)
            self.assertListEqual(data['response']['enabled_ranks'], enabled_ranks,
                    "Invalid enabled_ranks field: want={}, "
                    "got={}".format(enabled_ranks, data['response']['enabled_ranks']))

            self.log.debug("Checking disabled ranks state information")
            data = self.dmg.pool_query(self.pool.identifier, show_disabled=True)
            self.assertListEqual(data['response']['disabled_ranks'], disabled_ranks,
                    "Invalid disabled_ranks field: want={}, "
                    "got={}".format(disabled_ranks, data['response']['disabled_ranks']))
