#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

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
from apricot import TestWithServers
from general_utils import TestPool


class RebuildNoCap(TestWithServers):
    """Test class for failed pool rebuild.

    Test Class Description:
        This class contains tests for pool rebuild.

    :avocado: recursive
    """

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
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        targets = self.params.get("targets", "/run/server_config/*")
        data = self.params.get("datasize", "/run/testparams/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")

        # Create a pool
        self.pool.create()

        # Display pool size before write
        self.pool.display_pool_daos_space("before write")

        # Write enough data to the pool that will not be able to be rebuilt
        self.pool.write_file(
            self.orterun, len(self.hostlist_clients), self.hostfile_clients,
            data)

        # Display pool size after write
        self.pool.display_pool_daos_space("after write")

        # Verify the pool information before starting rebuild
        checks = {
            "pi_nnodes": len(self.hostlist_servers),
            "pi_ntargets": len(self.hostlist_servers) * targets,
            "pi_ndisabled": 0,
        }
        self.assertTrue(
            self.pool.check_pool_info(**checks),
            "Invlaid pool information detected before rebuild")
        self.assertTrue(
            self.pool.check_rebuild_status(rs_errno=0),
            "Invlaid pool rebuild error number detected before rebuild")

        # Kill the server
        self.pool.start_rebuild(self.server_group, rank, self.d_log)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True)

        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        # Display pool size after rebuild
        self.pool.display_pool_daos_space("after rebuild")

        # Verify the pool information after rebuild
        checks["pi_ndisabled"] = targets
        self.assertTrue(
            self.pool.check_pool_info(**checks),
            "Invalid pool information detected after rebuild")
        self.assertFalse(
            self.pool.check_rebuild_status(rs_errno=0),
            "Invalid pool rebuild error number detected after rebuild")
        self.log.info("Test Passed")
