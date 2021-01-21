#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers, skipForTicket
from test_utils_pool import TestPool
from test_utils_container import TestContainer


class RebuildWithIO(TestWithServers):
    """Test class for pool rebuild during I/O.

    Test Class Description:
        This class contains tests for pool rebuild that feature I/O going on
        during the rebuild.

    :avocado: recursive
    """

    @skipForTicket("DAOS-5611")
    def test_rebuild_with_io(self):
        """JIRA ID: Rebuild-003.

        Test Description:
            Trigger a rebuild while I/O is ongoing.

        Use Cases:
            single pool, single client performing continuous read/write/verify
            sequence while failure/rebuild is triggered in another process

        :avocado: tags=all,pool,rebuild,daily_regression,medium,rebuildwithio
        """
        # Get the test params
        pool = TestPool(self.context, self.get_dmg_command())
        pool.get_params(self)
        container = TestContainer(pool)
        container.get_params(self)
        targets = self.params.get("targets", "/run/server_config/*")
        # data = self.params.get("datasize", "/run/testparams/*")
        rank = self.params.get("rank", "/run/testparams/*")
        obj_class = self.params.get("object_class", "/run/testparams/*")
        server_count = len(self.hostlist_servers)

        # Create a pool and verify the pool info before rebuild (also connects)
        pool.create()
        status = pool.check_pool_info(
            pi_nnodes=server_count,
            pi_ntargets=(server_count * targets),  # DAOS-2799
            pi_ndisabled=0,
        )
        status &= pool.check_rebuild_status(
            rs_done=1, rs_obj_nr=0, rs_rec_nr=0, rs_errno=0)
        self.assertTrue(status, "Error confirming pool info before rebuild")

        # Create and open the contaner
        container.create()

        # Write data to the container for 30 seconds
        self.log.info(
            "Wrote %s bytes to container %s",
            container.execute_io(30, rank, obj_class), container.uuid)

        # Determine how many objects will need to be rebuilt
        container.get_target_rank_lists(" prior to rebuild")

        # Trigger rebuild
        pool.start_rebuild([rank], self.d_log)

        # Wait for recovery to start
        pool.wait_for_rebuild(True)

        # Write data to the container for another 30 seconds
        self.log.info(
            "Wrote an additional %s bytes to container %s",
            container.execute_io(30), container.uuid)

        # Wait for recovery to complete
        pool.wait_for_rebuild(False)

        # Check the pool information after the rebuild
        status = status = pool.check_pool_info(
            pi_nnodes=server_count,
            pi_ntargets=(server_count * targets),  # DAOS-2799
            pi_ndisabled=targets,                  # DAOS-2799
        )
        status &= pool.check_rebuild_status(
            rs_done=1, rs_obj_nr=">0", rs_rec_nr=">0", rs_errno=0)
        self.assertTrue(status, "Error confirming pool info after rebuild")

        # Verify the data after rebuild
        self.assertTrue(
            container.read_objects(),
            "Data verification error after rebuild")
        self.log.info("Test Passed")
