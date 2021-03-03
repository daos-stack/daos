#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from avocado.core.exceptions import TestFail

from apricot import TestWithServers
from pydaos.raw import DaosApiError


class PoolSvc(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Tests svc argument while pool create.

    :avocado: recursive
    """

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

        # Verify the result - If the svc_params[1] == 0 the dmp pool create is
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
            self.assertTrue(999999 not in self.pool.svc_ranks,
                            "999999 is in the pool's service ranks.")
            self.assertEqual(len(self.pool.svc_ranks), svc_params[1],
                             "Length of pool scv rank list is not equal to " +
                             "the expected number of pool service members.")
            self.assertEqual(len(self.pool.svc_ranks),
                             len(set(self.pool.svc_ranks)),
                             "Duplicate values in returned rank list")

            if svc_params[1] > 2:
                self.pool.get_info()
                leader = self.pool.info.pi_leader
                all_svc_ranks = self.pool.svc_ranks.copy()
                all_svc_ranks.remove(leader)
                non_leader = all_svc_ranks[-1]

                # Stop the pool leader
                self.log.info("Stopping the pool leader: %s", leader)
                try:
                    self.pool.pool.pool_svc_stop()
                except DaosApiError as error:
                    self.log.info(error)
                    self.fail("Error issuing DaosPool.pool_svc_stop()")

                # Exclude the pool leader
                self.log.info("Excluding the pool leader: %s", leader)
                try:
                    self.pool.exclude([leader], self.d_log)
                except TestFail as error:
                    self.log.info(error)
                    self.fail(
                        "Error issuing TestPool.exclude([{}])".format(leader))
                self.server_managers[-1].update_expected_states(
                    [leader], "evicted")

                # Disconnect from the pool
                self.log.info("Disconnecting from the pool")
                try:
                    self.pool.disconnect()
                except TestFail as error:
                    self.log.info(error)
                    self.fail(
                        "Error issuing TestPool.disconnect() after excluding "
                        "the leader")

                # Connect to the pool
                self.log.info("Connecting from the pool")
                try:
                    self.pool.connect()
                except TestFail as error:
                    self.log.info(error)
                    self.fail("Error issuing TestPool.connect()")

                # Disconnect from the pool
                self.log.info("Disconnecting from the pool")
                try:
                    self.pool.disconnect()
                except TestFail as error:
                    self.log.info(error)
                    self.fail(
                        "Error issuing TestPool.disconnect() after connecting "
                        "to the pool")

                # Stop a pool non-leader
                self.log.info("Stopping a pool non-leader: %s", non_leader)
                try:
                    self.pool.start_rebuild([non_leader], self.test_log)
                except TestFail as error:
                    self.log.info(error)
                    self.fail(
                        "Error issuing TestPool.start_rebuild([{}])".format(
                            non_leader))
                self.server_managers[-1].update_expected_states(
                    [non_leader], "evicted")

                # Exclude a pool non-leader
                self.log.info("Excluding a pool non-leader: %s", non_leader)
                try:
                    self.pool.exclude([non_leader], self.d_log)
                except TestFail as error:
                    self.log.info(error)
                    self.fail(
                        "Error issuing TestPool.exclude([{}])".format(
                            non_leader))
                self.server_managers[-1].update_expected_states(
                    [non_leader], "evicted")

                # Connect to the pool
                self.log.info("Connecting from the pool")
                try:
                    self.pool.connect()
                except TestFail as error:
                    self.log.info(error)
                    self.fail("Error issuing TestPool.connect()")

        self.log.info("Test passed!")
