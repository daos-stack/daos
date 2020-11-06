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
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from apricot import TestWithServers
from test_utils_pool import TestPool


class InfoTests(TestWithServers):
    """Test class for pool query.

    Test Class Description:
        This test verifies destruction of a pool that is rebuilding.

    :avocado: recursive
    """

    def test_pool_info_query(self):
        """Jira ID: DAOS-xxxx.

        Test Description:
            Create and connect to a pool.  Verify the pool information returned
            from the pool query matches the values used to create the pool.

        Use Cases:
            Verify pool query.

        :avocado: tags=all,tiny,pr,pool,smoke,infotest
        """
        # Get the test params
        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)
        permissions = self.params.get("permissions", "/run/test/*")
        targets = self.params.get("targets", "/run/server_config/*")
        #pool_targets = len(self.hostlist_servers) * targets

        # Create a pool
        self.pool.create()

        # Connect to the pool
        self.pool.connect(1 << permissions)

        # Verify the pool information
        checks = {
            "pi_uuid": self.pool.uuid,
            "pi_ntargets": len(self.hostlist_servers) * targets,
            "pi_nnodes": len(self.hostlist_servers),
            "pi_ndisabled": 0,
            "pi_map_ver": 1,
            "pi_leader": 0,
            "pi_bits": 0xFFFFFFFFFFFFFFFF,
        }
        status = self.pool.check_pool_info(**checks)
        self.assertTrue(status, "Invalid pool information detected prior")
        checks = {
            "s_total": (self.pool.scm_size.value, 0),
            #"s_free": (self.pool.scm_size.value - (256 * pool_targets), 0),
        }
        status = self.pool.check_pool_daos_space(**checks)
        self.assertTrue(status, "Invalid pool space information detected")
        self.log.info("Test Passed")
