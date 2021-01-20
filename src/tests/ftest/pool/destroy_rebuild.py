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


class DestroyRebuild(TestWithServers):
    """Test class for pool destroy tests.

    Test Class Description:
        This test verifies destruction of a pool that is rebuilding.

    :avocado: recursive
    """

    @skipForTicket("DAOS-2723")
    def test_destroy_while_rebuilding(self):
        """Jira ID: DAOS-xxxx.

        Test Description:
            Create a pool across multiple servers. After excluding one of the
            servers, verify that the pool can be destroyed during rebuild.

        Use Cases:
            Verifying that a pool can be destroyed during rebuild.

        :avocado: tags=all,daily_regression,medium,pool,destroypoolrebuild
        """
        # Get the test parameters
        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)
        targets = self.params.get("targets", "/run/server_config/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")

        # Create a pool
        self.pool.create()

        # Verify the pool information before starting rebuild
        checks = {
            "pi_nnodes": len(self.hostlist_servers),
            "pi_ntargets": len(self.hostlist_servers) * targets,
            "pi_ndisabled": 0,
        }
        self.assertTrue(
            self.pool.check_pool_info(**checks),
            "Invalid pool information detected prior to rebuild")

        # Start rebuild
        self.pool.start_rebuild([rank], self.d_log)
        self.pool.wait_for_rebuild(True)

        # Destroy the pool while rebuild is active
        self.pool.destroy()

        # Confirm the rebuild completes
        self.pool.wait_for_rebuild(False)
        self.log.info("Test Passed")
