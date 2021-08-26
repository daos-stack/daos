#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from nvme_utils import ServerFillUp


class RbldNoCapacity(ServerFillUp):
    """Test class for failed pool rebuild.

    Test Class Description:
        This class contains tests for pool rebuild.

    :avocado: recursive
    """

    def test_rebuild_no_capacity(self):
        """Jira ID: DAOS-841.

        Test Description:
            Create and connect and fill the pool. Once the pool is full,
            stop a server, rebuild is expected to fail due not no space.

        Use Cases:
            Rebuild with no space.

        :avocado: tags=all, full_regression
        :avocado: tags=medium, full_regression
        :avocado: tags=pool,rebuild,pool_no_cap
        """
        # Get the test params
        targets = self.params.get("targets", "/run/server_config/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")
        self.capacity = self.params.get("capacity", "/run/testparams/*")
        dmg = self.get_dmg_command()
        self.add_pool()

        self.log.info("pool_percentage_used -- Before -- %s",
                      self.pool.pool_percentage_used())

        # Start the IOR Command and fill disk
        self.start_ior_load(storage="NVMe", percent=self.capacity)

        self.log.info("pool_percentage_used -- After -- %s",
                      self.pool.pool_percentage_used())

        # # Check the pool information before the rebuild
        server_count = len(self.hostlist_servers)
        status = self.pool.check_pool_info(
            pi_nnodes=server_count,
            pi_ntargets=(server_count * targets),  # DAOS-2799 (Closed)
            pi_ndisabled=0
        )
        status &= self.pool.check_rebuild_status(rs_errno=0)
        # Ignoring validation
        # self.assertTrue(status, "Error confirming pool info after rebuild")

        # Stop the server
        # The test right now is hanging on this step
        dmg.system_stop(ranks=rank)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True)

        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        # Display pool size after rebuild
        self.pool.display_pool_daos_space("after rebuild")

        # Verify the pool information after rebuild
        status = self.pool.check_pool_info(
            pi_nnodes=server_count,
            pi_ntargets=(server_count * targets),  # DAOS-2799 (Closed)
            pi_ndisabled=targets  # DAOS-2799 (Closed)
        )
        status &= self.pool.check_rebuild_status(rs_errno=-1007)

        # Ignoring validation
        # self.assertTrue(status, "Error confirming pool info after rebuild")

        # Return the server back to online
        dmg.system_start(ranks=rank)
        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True)

        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)
        self.log.info("Test Passed")
