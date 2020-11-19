#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Governments rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
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

        :avocado: tags=all,pool,rebuild,pr,medium,rebuildwithio
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
