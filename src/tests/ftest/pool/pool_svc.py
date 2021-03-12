#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from avocado.core.exceptions import TestFail

from apricot import TestWithServers


class PoolSvc(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Tests svc argument while pool create.

    :avocado: recursive
    """

    def check_leader(self, previous_leader=None, expect_change=True):
        """Check if the pool leader rank has changed.

        Args:
            previous_leader (int, optional): previous pool leader rank. Defaults
                to None.
            expect_change (bool, optional): is the pool leader expected to
                change. Defaults to True.

        Return:
            int: current pool leader rank

        """
        self.pool.set_query_data()
        current_leader = int(self.pool.query_data["leader"])
        if previous_leader is not None:
            self.log.info(
                "Pool leader: previous=%s, current=%s",
                previous_leader, current_leader)
            leader_change = previous_leader != current_leader
            message = "The pool leader {} changed".format(
                "has" if leader_change else "has not")
            self.log.info("  %s", message)
            if leader_change != expect_change:
                self.fail(message)
        else:
            self.log.info("Pool leader: current=%s", current_leader)
        return current_leader

    def test_pool_svc(self):
        """Test svc arg during pool create.

        :avocado: tags=all,daily_regression
        :avocado: tags=medium
        :avocado: tags=pool,pool_svc,test_pool_svc,svc
        :avocado: tags=DAOS_5610
        """
        # parameter used in pool create
        svc_params = self.params.get("svc_params")

        # Setup the TestPool object
        self.add_pool(create=False)

        # Assign the expected svcn value
        if svc_params[0] != "None":
            self.pool.svcn.update(svc_params[0], "svcn")

        # Create the pool
        pool_create_error = None
        try:
            self.pool.create()
        except TestFail as error:
            pool_create_error = error

        # Verify the result - If the svc_params[1] == 0 the dmg pool create is
        # expected to fail
        if svc_params[1] == 0 and pool_create_error:
            self.log.info(
                "Pool creation with svcn=%s failed as expected", svc_params[0])
        elif pool_create_error:
            self.fail(
                "Pool creation with svcn={} failed when it was expected to "
                "pass: {}".format(svc_params[0], pool_create_error))
        else:
            self.log.info("Pool creation passed as expected")
            self.log.info(
                "Verifying that the pool has %s pool service members",
                svc_params[1])
            self.log.info("  self.pool.svc_ranks = %s", self.pool.svc_ranks)

            # Verify the pool service member list:
            #   - does not contain an invalid rank
            #   - contains the expected number of members
            #   - does not contain any duplicate ranks
            self.assertTrue(
                999999 not in self.pool.svc_ranks,
                "999999 is in the pool's service ranks.")
            self.assertEqual(
                len(self.pool.svc_ranks), svc_params[1],
                "Length of pool scv rank list is not equal to the expected "
                "number of pool service members.")
            self.assertEqual(
                len(self.pool.svc_ranks),
                len(set(self.pool.svc_ranks)),
                "Duplicate values in returned rank list")

            if svc_params[1] > 2:
                # Query the pool to get the leader
                pool_leader = self.check_leader()
                non_leader_ranks = list(self.pool.svc_ranks)
                non_leader_ranks.remove(pool_leader)

                # Stop the pool leader
                self.log.info("Stopping the pool leader: %s", pool_leader)
                try:
                    self.server_managers[-1].stop_ranks(
                        [pool_leader], self.test_log)
                except TestFail as error:
                    self.log.info(error)
                    self.fail(
                        "Error stopping pool leader - "
                        "DaosServerManager.stop_ranks([{}])".format(
                            pool_leader))

                # Verify the pool leader has changed
                pool_leader = self.check_leader(pool_leader, True)
                non_leader_ranks.remove(pool_leader)

                # Stop a pool non-leader
                non_leader = non_leader_ranks[-1]
                self.log.info(
                    "Stopping a pool non-leader (%s): %s",
                    non_leader_ranks, non_leader)
                try:
                    self.server_managers[-1].stop_ranks(
                        [non_leader], self.test_log)
                except TestFail as error:
                    self.log.info(error)
                    self.fail(
                        "Error stopping a pool non-leader - "
                        "DaosServerManager.stop_ranks([{}])".format(non_leader))

                # Verify the pool leader has not changed
                self.check_leader(pool_leader, False)

        self.log.info("Test passed!")
