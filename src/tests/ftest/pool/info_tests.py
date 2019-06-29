#!/usr/bin/python
"""
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
"""
# import os

from apricot import TestWithServers
# from conversion import c_uuid_to_str
# from daos_api import DaosPool
# from general_utils import get_pool
from general_utils import TestPool


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
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        permissions = self.params.get("permissions", "/run/test/*")
        targets = self.params.get("targets", "/run/server_config/*")
        pool_targets = len(self.hostlist_servers) * targets

        # Create a pool
        self.pool.create()

        # Connect to the pool
        self.pool.connect(permissions)

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
        self.assertTrue(status, "Invlaid pool information detected prior")
        checks = {
            "s_total": (self.pool.scm_size.value, 0),
            "s_free": (self.pool.scm_size.value - (256 * pool_targets), 0),
        }
        status = self.pool.check_pool_daos_space(**checks)
        self.assertTrue(status, "Invlaid pool space information detected")
        self.log.info("Test Passed")

        # mode = self.params.get("mode", "/run/pool/*")
        # size = self.params.get("size", "/run/pool/*")
        # permissions = self.params.get("permissions", "/run/pool/*")
        # targets = self.params.get("targets", "/run/server_config/*")
        # uid = os.geteuid()
        # gid = os.getegid()
        # connect_flags = 1 << permissions
        # pool_nodes = len(self.hostlist_servers)
        # pool_targets = targets * pool_nodes

        # # Expected pool total and free space.
        # # For tmpfs space is only reported for one media type
        # total = (size, 0)
        # free = (size - (256 * pool_targets), 0)

        # # Create a pool
        # self.log.info("Creating a pool with mode %s", mode)
        # self.pool = get_pool(
        #     self.context, mode, size, self.server_group, log=self.log,
        #     connect=False)
        # pool_uuid = self.pool.get_uuid_str()

        # # Connect to the pool
        # self.log.info(
        #     "Connecting to pool %s with flags %s", pool_uuid, connect_flags)
        # self.pool.connect(connect_flags)

        # # Verif the pool query information
        # self.log.info("Querying the pool information")
        # pool_info = self.pool.pool_query()
        # check_list = (
        #     ("UUID", c_uuid_to_str(pool_info.pi_uuid), pool_uuid),
        #     ("number of targets", pool_info.pi_ntargets, pool_targets),
        #     ("number of nodes", pool_info.pi_nnodes, pool_nodes),
        #     ("number of disabled targets", pool_info.pi_ndisabled, 0),
        #     ("map version", pool_info.pi_map_ver, 1),
        #     ("leader", pool_info.pi_leader, 0),
        #     ("bits", pool_info.pi_bits, 0xFFFFFFFFFFFFFFFF),
        #     ("total space", pool_info.pi_space.ps_space.s_total[0], total[0]),
        #     ("total space", pool_info.pi_space.ps_space.s_total[1], total[1]),
        #     ("free space", pool_info.pi_space.ps_space.s_free[0], free[0]),
        #     ("free space", pool_info.pi_space.ps_space.s_free[1], free[1]),
        # )
        # check_status = True
        # for check, actual, expect in check_list:
        #     self.log.info(
        #         "Verifying the pool %s: %s ?= %s", check, actual, expect)
        #     if actual != expect:
        #         msg = "The {} does not match: actual: {}, expected: {}".format(
        #             check, actual, expect)
        #         self.d_log.error(msg)
        #         self.log.error(msg)
        #         check_status = False

        # self.assertTrue(check_status, "Error validating the pool information")
        # self.log.info("Test Passed")
