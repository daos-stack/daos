#!/usr/bin/python
'''
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
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from apricot import TestWithServers, skipForTicket
from test_utils_pool import TestPool


class RebuildNoCap(TestWithServers):
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

        :avocado: tags=all,medium,pr,pool,rebuild,nocap
        """
        # Get the test params
        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)
        targets = self.params.get("targets", "/run/server_config/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")

        # Create a pool
        self.pool.create()

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
        self.pool.start_rebuild([rank], self.d_log)

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
