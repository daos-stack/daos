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
from __future__ import print_function

from apricot import TestWithServers
from daos_api import DaosApiError
from test_utils import TestPool, TestContainer
from conversion import c_uuid_to_str
import ctypes
import uuid


class EvictTests(TestWithServers):
    """

    Tests DAOS client eviction from a pool that the client is using.

    :avocado: recursive
    """

    def connected_pool(self, hostlist, targets=None):
        """Connect to pool.

        Args:
            hostlist (list): list of daos server nodes
            targets (list): List of targets for pool create
        Returns:
            TestPool (object)

        """
        pool = TestPool(self.context, self.log)
        pool.get_params(self)
        if targets is not None:
            pool.target_list.value = targets
        # create pool
        pool.create()
        # Check that the pool was created
        status = pool.check_files(hostlist)
        if not status:
            self.fail("Invalid pool - pool data not detected on servers")
        # Connect to the pool
        status = pool.connect(1)
        if not status:
            self.fail("Pool connect failed or already connected")
        # Return connected pool
        return pool

    def evict_badparam(self, test_param):
        """Connect to pool, connect and try to evict with a bad param.

        Args:
            test_param (str): either invalid UUID or bad server name

        Returns:
            TestPool (bool)

        """
        # setup pool and connect
        self.pool = self.connected_pool(self.hostlist_servers)

        self.log.info(
            "Pool UUID: %s\n Pool handle: %s\n Server group: %s\n",
            self.pool.uuid, self.pool.pool.handle.value, self.pool.name)

        if test_param == "BAD_SERVER_NAME":
            # Attempt to evict pool with invald server group name
            # set the server group name directly
            self.pool.pool.group = ctypes.create_string_buffer(test_param)
            self.log.info(
                "Evicting pool with invalid Server Group Name: %s", test_param)
        elif test_param == "invalid_uuid":
            # Attempt to evict pool with invald UUID
            bogus_uuid = self.pool.uuid
            # in case uuid4() generates pool.uuid
            while bogus_uuid == self.pool.uuid:
                bogus_uuid = str(uuid.uuid4())
            # set the UUID directly
            self.pool.pool.set_uuid_str(bogus_uuid)
            self.log.info(
                "Evicting pool with Invalid Pool UUID:  %s",
                self.pool.pool.get_uuid_str())
        else:
            self.fail("Invalid yaml parameters - check \"params\" values")
        try:
            # call daos evict api directly
            self.pool.pool.evict()
        # exception is expected
        except DaosApiError as result:
            status = "-1005" in str(result)
            if status:
                self.log.info(
                    "Expected exception - invalid param %s\n %s\n",
                    test_param, str(result))
            else:
                self.log.info(
                    "Unexpected exception - invalid param %s\n %s\n",
                    test_param, str(result))
            # Restore the valid server group name or uuid and verify that
            # pool still exists and the handle is still valid.
            if "BAD_SERVER_NAME" in test_param:
                self.pool.pool.group = ctypes.create_string_buffer(
                    self.server_group)
            else:
                self.pool.pool.set_uuid_str(self.pool.uuid)

            self.log.info("Check if pool handle still exist")
            if int(self.pool.pool.handle.value) == 0:
                self.log.error(
                    "Pool handle was removed when evicting pool with %s",
                    test_param)
                status &= False
            return status
        # if here then pool-evict did not raise an exception as expected
        # restore the valid server group name and check if valid pool
        # still exists
        self.log.info(
            "DAOS api exception did not occur"
            " - evict from pool with %s", test_param)

        # restore the valid group name and UUID,
        if "BAD_SERVER_NAME" in test_param:
            self.pool.pool.group = ctypes.create_string_buffer(
                self.server_group)
        else:
            self.pool.pool.set_uuid_str(self.pool.uuid)
        # check if pool handle still exists
        if int(self.pool.pool.handle.value) == 0:
            self.log.error(
                "Pool handle was removed when doing an evict with bad param")
        if self.pool.check_files(self.hostlist_servers):
            self.log.error("Valid pool files were not detected on server after"
                           " a pool evict with %s failed to raise an "
                           "exception", test_param)
        self.log.error("Test did not raise an exception with when "
                       "evicting a pool with bad param: %s", test_param)
        return False

    def test_evict(self):
        """
        Test evicting a client from a pool.

        Test creates 2 pools on 4 target (pool_tgt [0,1,2,3])
        and 1 pool on 2 targets (pool_tgt_ut [0,1]).  The pools are connected
        to and a container with data is created on all 3.
        The evict is done on connection to the pool with 2 targets.
        The handle is removed.
        The test verifies that the other two pools were not affected
        by the evict
        :avocado: tags=all,pool,pr,full_regression,small,poolevict
        """
        pool = []
        container = []
        non_pool_servers = []
        # Target list is configured so that the pools are across all servers
        # except the pool under test is created on half of the servers
        pool_tgt = [num for num in range(len(self.hostlist_servers))]
        pool_tgt_ut = [num for num in range(int(len(self.hostlist_servers)/2))]
        tlist = [pool_tgt, pool_tgt, pool_tgt_ut]
        pool_servers = [self.hostlist_servers[:len(tgt)] for tgt in tlist]
        non_pool_servers = [self.hostlist_servers[len(tgt):] for tgt in tlist]
        # Create Connected TestPool
        for count, target_list in enumerate(tlist):
            pool.append(self.connected_pool(pool_servers[count], target_list))
            if len(non_pool_servers[count]) > 0:
                self.assertFalse(
                    pool[count].check_files(non_pool_servers[count]),
                    "Pool # {} data detected on non pool servers {} ".format(
                        count+1, non_pool_servers[count]))

            self.log.info("Pool # %s is connected with handle %s",
                          count+1, pool[count].pool.handle.value)

            # Create a container
            # container.append(get_container(
            #     self.context, pool[count].pool, self.log))
            # cont_uuid.append(container[count].get_uuid_str())
            # self.log.info("Pool # %s has container %s",
            #               count+1, cont_uuid[count])
            container.append(TestContainer(pool[count]))
            container[count].get_params(self)
            container[count].create()
            container[count].write_objects(target_list[-1])

        try:
            self.log.info(
                "Attempting to evict clients from pool with UUID: %s",
                pool[-1].uuid)
            # Evict the last pool in the list
            pool[-1].pool.evict()
        except DaosApiError as result:
            self.fail(
                "Detected exception while evicting a client {}".format(
                    str(result)))

        for count in range(len(tlist)):
            # Check that all pool files still exist
            if pool[count].check_files(pool_servers[count]):
                self.log.info(
                    "Pool # %s with UUID %s still exists",
                    count+1, pool[count].uuid)
            else:
                self.fail(
                    "Pool # {} with UUID {} does not exists".format(
                        count+1, pool[count].uuid))

            # Verify connection to pools with pool_query; pool that was evicted
            # should fail the pool query because the handle was removed
            try:
                # Call daos api directly to avoid connecting to pool
                pool_info = pool[count].pool.pool_query()
            except DaosApiError as error:
                # expected error for evicted pool
                if count == len(tlist) - 1 and "-1002" in str(error):
                    self.log.info(
                        "Pool # %s was unable to query pool info due to "
                        "expected invalid handle error (-1002):\n\t%s",
                        count+1, error)
                # unexpected error from pool_query
                else:
                    self.fail(
                        "Pool # {} failed pool query: {}".format(
                            count+1, error))

                pool_info = None
            # Check that UUID of valid pools still exists
            if pool_info:
                if c_uuid_to_str(pool_info.pi_uuid) == pool[count].uuid:
                    self.log.info(
                        "Pool # %s UUID matches pool_info.pi_uuid %s",
                        count+1, pool[count].uuid)
                else:
                    self.fail(
                        "Pool # {} UUID does not matches pool_info.pi_uuid:  "
                        "{} != {}".format(
                            count+1, pool[count].uuid, c_uuid_to_str(
                                pool_info.pi_uuid)))

    def test_evict_bad_server_name(self):
        """
        Test evicting a pool using an invalid server group name.

        :avocado: tags=all,pool,pr,full_regression,small,poolevict
        :avocado: tags=poolevict_bad_server_name
        """
        test_param = self.params.get("server_name", '/run/badparams/*')
        self.assertTrue(self.evict_badparam(test_param))

    def test_evict_bad_uuid(self):
        """
        Test evicting a pool using an invalid uuid.

        :avocado: tags=all,pool,pr,full_regression,small,poolevict
        :avocado: tags=poolevict_bad_uuid
        """
        test_param = self.params.get("uuid", '/run/badparams/*')
        self.assertTrue(self.evict_badparam(test_param))
