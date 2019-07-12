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
  The Governments rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from apricot import TestWithServers
from general_utils import TestPool, TestContainer


class RebuildTests(TestWithServers):
    """Test class for rebuild tests.

    Test Class Description:
        This class contains tests for pool rebuild.

    :avocado: recursive
    """

    def test_simple_rebuild(self):
        """JIRA ID: Rebuild-001.

        Test Description:
            The most basic rebuild test.

        Use Cases:
            single pool rebuild, single client, various reord/object counts

        :avocado: tags=all,pr,medium,pool,rebuild,rebuildsimple
        """
        # Get the test parameters
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.container = TestContainer(self.pool)
        self.container.get_params(self)
        rank = self.params.get("rank", "/run/testparams/*")

        # Create a pool and confirm its status
        self.pool.create()
        self.pool.check_pool_info(pi_ndisabled=0)
        self.pool.check_rebuild_status(
            rs_done=1, rs_obj_nr=0, rs_rec_nr=0, rs_errno=0)

        # Create a container in this pool and fill it with data
        self.container.create()
        self.container.write_objects()

        # Kill the server
        self.pool.start_rebuild(self.server_group, rank, self.d_log)

        # Wait for recovery to start
        self.pool.wait_for_rebuild(True)

        # Wait for recovery to complete
        self.pool.wait_for_rebuild(False)

        # Check the pool information after the rebuild
        # self.pool.check_pool_info(pi_ndisabled=targets)  DAOS-2799
        rs_obj_nr = self.container.object_qty.value
        rs_rec_nr = rs_obj_nr * self.container.record_qty.value
        self.pool.check_rebuild_status(
            rs_done=1, rs_obj_nr=rs_obj_nr, rs_rec_nr=rs_rec_nr, rs_errno=0)

        # Verify the data after rebuild
        self.assertTrue(
            self.container.read_objects(),
            "Data verifiaction error after rebuild")
        self.log.info("Test Passed")

    def test_multipool_rebuild(self):
        """JIRA ID: DAOS-XXXX (Rebuild-002).

        Test Description:
            Expand on the basic test by rebuilding 2 pools at once.

        Use Cases:
            multipool rebuild, single client, various object and record counds

        :avocado: tags=all,pr,medium,pool,rebuild,rebuildmulti
        """
        # Get the test parameters
        pools = []
        containers = []
        rank = self.params.get("rank", "/run/testparams/*")
        quantity = self.params.get("quantity", "/run/testparams/*")
        for index in range(quantity):
            pools.append(TestPool(self.context, self.log))
            pools[index].get_params(self)
            containers.append(TestContainer(pools[index]))
            containers[index].get_params(self)

        # Create the pools and confirm their status
        for index in range(quantity):
            pools[index].create()
            pools[index].check_pool_info(pi_ndisabled=0)
            pools[index].check_rebuild_status(
                rs_done=1, rs_obj_nr=0, rs_rec_nr=0, rs_errno=0)

        # Create containers in each pool and fill it with data
        for index in range(quantity):
            containers[index].create()
            containers[index].write_objects()

        # Kill the server
        pools[0].start_rebuild(self.server_group, rank, self.d_log)
        pools[1].exclude(rank, self.d_log)

        # Wait for recovery to start
        for index in range(quantity):
            pools[index].wait_for_rebuild(True)

        # Wait for recovery to complete
        for index in range(quantity):
            pools[index].wait_for_rebuild(False)

        # Check the pool information after the rebuild
        for index in range(quantity):
            # pools[index].check_pool_info(pi_ndisabled=targets)  DAOS-2799
            rs_obj_nr = containers[index].object_qty.value
            rs_rec_nr = rs_obj_nr * containers[index].record_qty.value
            pools[index].check_rebuild_status(
                rs_done=1, rs_obj_nr=rs_obj_nr, rs_rec_nr=rs_rec_nr,
                rs_errno=0)

        # Verify the data after rebuild
        for index in range(quantity):
            self.assertTrue(
                containers[index].read_objects(),
                "Data verifiaction error after rebuild")
        self.log.info("Test Passed")
