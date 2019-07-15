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
from __future__ import print_function

from apricot import TestWithServers
from daos_api import DaosApiError
from general_utils import TestPool, get_container
from conversion import c_uuid_to_str
import ctypes


class EvictTests(TestWithServers):
    """
    Tests DAOS client eviction from a pool that the client is using.

    :avocado: recursive
    """

    def test_evict(self):
        """
        Test evicting a client from a pool.

        :avocado: tags=pool,poolevict,quick
        """
        pool = []
        container = []
        cont_uuid = []
        non_pool_servers = []
        # Target list is configured so that the pools are across all servers
        # except the pool under test is created on half of the servers
        pool_tgt = [num for num in range(len(self.hostlist_servers))]
        pool_tgt_ut = [num for num in range(int(len(self.hostlist_servers)/2))]
        tlist = [pool_tgt, pool_tgt, pool_tgt_ut]
        pool_servers = [self.hostlist_servers[:len(tgt)] for tgt in tlist]
        # Create TestPool
        for count, _ in enumerate(tlist):
            self.log.info("Create a pool")
            pool.append(TestPool(self.context, self.log))
            pool[count].get_params(self)
            pool[count].name.value = self.server_group
            pool[count].target_list.value = tlist[count]
            self.log.info(
                "Pool # %s target list is %s",
                count+1, pool[count].target_list)
            pool[count].create()
            self.log.info("Pool # %s UUID is %s and handle is %s",
                          count+1, pool[count].uuid,
                          pool[count].pool.handle.value)

            # Check that the pools exist
            self.assertTrue(
                pool[count].check_files(pool_servers[count]),
                "Pool # {} data not detected on servers before evict".format(
                    count+1))
            # Check that pool does not exists on targets not in pool
            for host in self.hostlist_servers:
                if host not in pool_servers[count]:
                    non_pool_servers.append(host)
            if len(non_pool_servers) > 0:
                self.assertFalse(
                    pool[count].check_files(non_pool_servers),
                    "Pool # {} data detected on non pool servers {} ".format(
                        count+1, non_pool_servers))

            # Connect to each pool
            self.assertTrue(pool[count].connect(1 << 1),
                            "Pool connect failed before evict")
            self.log.info("Pool # %s is connected with handle %s",
                          count+1, pool[count].pool.handle.value)

            # Create a container
            container.append(get_container(
                self.context, pool[count].pool, self.log))
            cont_uuid.append(container[count].get_uuid_str())
            self.log.info("Pool # %s has container %s",
                          count+1, cont_uuid[count])

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
            # Check that all pools still exist
            if pool[count].check_files(pool_servers[count]):
                self.log.info(
                    "Pool # %s with UUID %s still exists",
                    count+1, pool[count].uuid)
            else:
                self.fail(
                    "Pool # {} with UUID {} does not exists".format(
                        count+1, pool[count].uuid))

            # Verify connection to pools with pool_query
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

    def test_evict_bad_uuid(self):
        """
        Test evicting a pool with an invalid uuid.

        :avocado: tags=pool,poolevict
        """
        # Create a pool
        self.log.info("Create a pool")
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.create()
        self.log.info("Valid Pool UUID is %s", self.pool.uuid)

        # Check that the pool was created
        self.assertTrue(
            self.pool.check_files(self.hostlist_servers),
            "Pool data not detected on servers before evict")

        # Connect to the pool
        self.assertTrue(self.pool.connect(1),
                        "Pool connect failed before evict")
        self.log.info(
            "Pool UUID is %s and handle is %s",
            self.pool.uuid, self.pool.pool.handle.value)

        # Attempt to evict pool with invald UUID
        saved_uuid = self.pool.uuid
        bogus_uuid = '81ef94d7-a59d-4a5e-935b-abfbd12f2105'
        # set the UUID directly
        self.pool.pool.set_uuid_str(bogus_uuid)
        self.log.info("Evicting pool with Invalid Pool UUID:  %s",
                      self.pool.pool.get_uuid_str())
        try:
            self.log.info("Attempting to evict pool with an invalid UUID")
            # call daos evict api directly
            self.pool.pool.evict()
        # exception is expected
        except DaosApiError as result:
            self.log.info(
                "Expected exception - evicting pool with invalid UUID\n %s",
                str(result))
            # restore the valid UUID and check if pool handle still exists
            self.pool.pool.set_uuid_str(saved_uuid)
            self.log.info("Check if pool handle still exist")
            self.assertTrue(
                (int(self.pool.pool.handle.value) > 0),
                "Pool handle was removed when doing an evict with a bad UUID")
            return
        # if here then pool-evict did not raise an exception as expected
        # restore the valid UUID and check if valid pool still exists
        self.log.info(
            "DAOS api exception did not occur"
            " - evict pool with invalid UUID")
        self.pool.pool.set_uuid_str(saved_uuid)
        self.log.info("check if pool data and pool handle still exist")
        self.assertTrue(
            (int(self.pool.pool.handle.value) > 0),
            "Pool handle was removed when doing an evict with a bad UUID")
        self.assertTrue(
            self.pool.check_files(self.hostlist_servers),
            "Valid pool data was not detected on servers after "
            "a pool evict with invalid UUID failed to raise an exception")
        self.fail(
            "Test did not raise an exception when "
            "evicting a pool with invalid UUID")

    def test_evict_bad_server_group(self):
        """
        Test evicting a pool using an invalid server group name.

        :avocado: tags=pool,poolevict
        """
        setid = self.params.get("setname", '/run/setnames/validsetname/')
        badsetid = self.params.get("setname", '/run/setnames/badsetname/')

        # Create a pool
        self.log.info("Create a pool")
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)
        self.pool.name.value = setid
        self.pool.create()
        self.log.info("Pool created with Server Group Name: %s",
                      self.pool.name.value)
        # Check that the pool was created
        self.assertTrue(
            self.pool.check_files(self.hostlist_servers),
            "Pool data not detected on servers before evict")

        # Connect to the pool
        self.assertTrue(self.pool.connect(1),
                        "Pool connect failed before evict")
        self.log.info(
            "Pool UUID is %s and handle is %s",
            self.pool.uuid, self.pool.pool.handle.value)

        # Attempt to evict pool with invald server group name
        # set the server group name directly
        self.pool.pool.group = ctypes.create_string_buffer(badsetid)
        self.log.info(
            "Evicting pool with invalid Server Group Name:  %s", badsetid)
        try:
            # call daos evict api directly
            self.pool.pool.evict()
        # exception is expected
        except DaosApiError as result:
            self.log.info(
                "Expected exception - evicting pool with "
                "invalid Server Group\n %s", str(result))
            # restore the valid group name, check if pool handle still exists
            self.pool.pool.group = ctypes.create_string_buffer(setid)
            self.log.info("Check if pool handle still exist")
            self.assertTrue(
                (int(self.pool.pool.handle.value) > 0),
                "Pool handle was removed when evicting pool with "
                "invalid server group name")
            return
        # if here then pool-evict did not raise an exception as expected
        # restore the valid server group name and check if valid pool
        # still exists
        self.log.info(
            "DAOS api exception did not occur"
            " - evict pool with invalid Server Group Name")
        self.pool.pool.group = ctypes.create_string_buffer(setid)
        self.log.info("check if pool data and pool handle still exist")
        self.assertTrue(
            (int(self.pool.pool.handle.value) > 0),
            "Pool handle was removed when doing an evict with a bad "
            "invalid Server Group Name")
        self.assertTrue(
            self.pool.check_files(self.hostlist_servers),
            "Valid pool data was not detected on servers after "
            "a pool evict with invalid Server Group Name failed "
            "to raise an exception")
        self.fail(
            "Test did not raise an exception when "
            "evicting a pool with invalid Server Group Name")
