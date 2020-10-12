#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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
import time
import random
import ctypes
from avocado import fail_on
from apricot import TestWithServers
from test_utils_pool import TestPool
from command_utils import CommandFailure
from pydaos.raw import (DaosContainer, IORequest,
                        DaosObj, DaosApiError)


class OSAOfflineDrain(TestWithServers):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline drain test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(OSAOfflineDrain, self).setUp()
        self.dmg_command = self.get_dmg_command()
        self.no_of_dkeys = self.params.get("no_of_dkeys", '/run/dkeys/*')[0]
        self.no_of_akeys = self.params.get("no_of_akeys", '/run/akeys/*')[0]
        self.record_length = self.params.get("length", '/run/record/*')[0]

    @fail_on(CommandFailure)
    def get_pool_leader(self):
        """Get the pool leader.

        Returns:
            int: pool leader value

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return int(data["leader"])

    @fail_on(CommandFailure)
    def get_pool_version(self):
        """Get the pool version.

        Returns:
            int: pool_version_value

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return int(data["version"])

    @fail_on(DaosApiError)
    def write_single_object(self):
        """Write some data to the existing pool."""
        self.pool.connect(2)
        csum = self.params.get("enable_checksum", '/run/container/*')
        container = DaosContainer(self.context)
        input_param = container.cont_input_values
        input_param.enable_chksum = csum
        container.create(poh=self.pool.pool.handle,
                         con_prop=input_param)
        container.open()
        obj = DaosObj(self.context, container)
        obj.create(objcls=1)
        obj.open()
        ioreq = IORequest(self.context,
                          container,
                          obj, objtype=4)
        self.log.info("Writing the Single Dataset")
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = ("{0}".format(str(akey)[0])
                          * self.record_length)
                d_key_value = "dkey {0}".format(dkey)
                c_dkey = ctypes.create_string_buffer(d_key_value)
                a_key_value = "akey {0}".format(akey)
                c_akey = ctypes.create_string_buffer(a_key_value)
                c_value = ctypes.create_string_buffer(indata)
                c_size = ctypes.c_size_t(ctypes.sizeof(c_value))
                ioreq.single_insert(c_dkey, c_akey, c_value, c_size)

    def run_offline_drain_test(self, num_pool, data=False):
        """Run the offline drain without data.
            Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
        """
        # Create a pool
        pool = {}
        pool_uuid = []
        target_list = []
        drain_servers = len(self.hostlist_servers) - 1

        # Exclude target : random two targets  (target idx : 0-7)
        n = random.randint(0, 6)
        target_list.append(n)
        target_list.append(n+1)
        t_string = "{},{}".format(target_list[0], target_list[1])

        # Drain a rank (or server)
        rank = random.randint(1, drain_servers)

        for val in range(0, num_pool):
            pool[val] = TestPool(self.context,
                                 dmg_command=self.get_dmg_command())
            pool[val].get_params(self)
            # Split total SCM and NVME size for creating multiple pools.
            pool[val].scm_size.value = int(pool[val].scm_size.value /
                                           num_pool)
            pool[val].nvme_size.value = int(pool[val].nvme_size.value /
                                            num_pool)
            pool[val].create()
            pool_uuid.append(pool[val].uuid)
            self.pool = pool[val]
            if data:
                self.write_single_object()

        # Drain the pool_uuid, rank and targets
        for val in range(0, num_pool):
            self.pool = pool[val]
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            output = self.dmg_command.pool_drain(self.pool.uuid,
                                                 rank, t_string)
            self.log.info(output)

            pver_drain = self.get_pool_version()
            fail_count = 0
            while fail_count <= 20:
                pver_drain = self.get_pool_version()
                time.sleep(10)
                fail_count += 1
                if pver_drain > pver_begin + 1:
                    break

            pver_drain = self.get_pool_version()
            self.log.info("Pool Version after drain %d", pver_drain)
            # Check pool version incremented after pool drain
            self.assertTrue(pver_drain > (pver_begin + 1),
                            "Pool Version Error:  After drain")

        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            pool[val].display_pool_daos_space(display_string)
            pool[val].destroy()

    def test_osa_offline_drain(self):
        """
        JIRA ID: DAOS-4750

        Test Description: Validate Offline Drain

        :avocado: tags=all,pr,hw,large,osa,osa_drain,offline_drain
        """
        # Perform drain testing with 1 to 2 pools
        # Two pool testing blocked by DAOS-5333.
        # Fix range from 1,2 to 1,3
        for pool_num in range(1, 2):
            self.run_offline_drain_test(pool_num)
        # Perform drain testing : inserting data in pool
        # Bug : DAOS-4946 blocks the following test case.
        # self.run_offline_drain_test(1, True)
