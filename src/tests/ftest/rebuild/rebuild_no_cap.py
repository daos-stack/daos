#!/usr/bin/python3
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import TestWithServers, skipForTicket


class RbldNoCapacity(TestWithServers):
    """Test class for failed pool rebuild.

    Test Class Description:
        This class contains tests for pool rebuild.

    :avocado: recursive
    """

    @skipForTicket("DAOS-2845")
    def test_rebuild_no_capacity(self):
        """Jira ID: DAOS-xxxx.

        Test Description:
            Create and connect to a pool.  Verify the pool information returned
            from the pool query matches the values used to create the pool.

        Use Cases:
            Verify pool query.

        :avocado: tags=all,daily_regression
        :avocado: tags=medium
        :avocado: tags=pool,rebuild,no_cap
        """
        # Get the test params
        targets = self.params.get("targets", "/run/server_config/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")

        # Create a pool
        self.add_pool()

        # Display pool size before write
        self.pool.display_pool_daos_space("before write")

        # Write enough data to the pool that will not be able to be rebuilt
        data = self.pool.scm_size.value * (targets - 1)
        self.pool.write_file(
            self.orterun, len(self.hostlist_clients), self.hostfile_clients,
            data)

        # Display pool size after write
        self.pool.display_pool_daos_space("after write")

        # Verify the pool information before starting rebuild

        # Check the pool information after the rebuild
        server_count = len(self.hostlist_servers)
        status = self.pool.check_pool_info(
            pi_nnodes=server_count,
            # pi_ntargets=(server_count * targets),  # DAOS-2799
            pi_ndisabled=0
        )
        status &= self.pool.check_rebuild_status(rs_errno=0)
        self.assertTrue(status, "Error confirming pool info after rebuild")

        # Kill the server
        self.server_managers[0].stop_ranks([rank], self.d_log)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True)

        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        # Display pool size after rebuild
        self.pool.display_pool_daos_space("after rebuild")

        # Verify the pool information after rebuild
        status = self.pool.check_pool_info(
            pi_nnodes=server_count,
            # pi_ntargets=(server_count * targets),  # DAOS-2799
            # pi_ndisabled=targets                   # DAOS-2799
        )
        status &= self.pool.check_rebuild_status(rs_errno=-1007)
        self.assertTrue(status, "Error confirming pool info after rebuild")

        self.log.info("Test Passed")
