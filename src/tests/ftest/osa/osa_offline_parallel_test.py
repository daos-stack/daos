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
import threading
import copy
from avocado import fail_on
from apricot import TestWithServers
from test_utils_pool import TestPool
from command_utils import CommandFailure
from pydaos.raw import (DaosContainer, IORequest,
                        DaosObj, DaosApiError)
try:
    # python 3.x
    import queue as queue
except ImportError:
    # python 2.7
    import Queue as queue

class OSAOfflineParallelTest(TestWithServers):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline drain,reintegration,
    extend test cases in parallel.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(OSAOfflineParallelTest, self).setUp()
        self.dmg_command = self.get_dmg_command()
        self.no_of_dkeys = self.params.get("no_of_dkeys", '/run/dkeys/*')[0]
        self.no_of_akeys = self.params.get("no_of_akeys", '/run/akeys/*')[0]
        self.record_length = self.params.get("length", '/run/record/*')[0]
        self.out_queue = queue.Queue()
        self.test_ioreq = None

    @fail_on(CommandFailure)
    def get_pool_leader(self):
        """Get the pool leader
           Returns :
            int: pool leader value
        """
        kwargs = {"pool": self.pool.uuid}
        out = self.dmg_command.get_output("pool_query", **kwargs)
        return int(out[0][3])

    @fail_on(CommandFailure)
    def get_pool_version(self):
        """Get the pool version
           Returns :
            int : pool_version_value
        """
        kwargs = {"pool": self.pool.uuid}
        out = self.dmg_command.get_output("pool_query", **kwargs)
        return int(out[0][4])

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
        self.test_ioreq = ioreq
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

    @fail_on(DaosApiError)
    def verify_single_object(self):
        """Verify the container data on the existing pool."""
        self.log.info("Single Dataset Verification -- Started")
        for dkey in range(self.no_of_dkeys):
            for akey in range(self.no_of_akeys):
                indata = ("{0}".format(str(akey)[0]) *
                          self.record_length)
                c_dkey = ctypes.create_string_buffer("dkey {0}".format(dkey))
                c_akey = ctypes.create_string_buffer("akey {0}".format(akey))
                val = self.test_ioreq.single_fetch(c_dkey,
                                                   c_akey,
                                                   len(indata)+1)
                if indata != (repr(val.value)[1:-1]):
                    self.d_log.error("ERROR:Data mismatch for "
                                     "dkey = {0}, "
                                     "akey = {1}".format(
                                         "dkey {0}".format(dkey),
                                         "akey {0}".format(akey)))
                    self.fail("ERROR: Data mismatch for dkey = {0}, akey={1}"
                              .format("dkey {0}".format(dkey),
                                      "akey {0}".format(akey)))

    def dmg_thread(self, puuid, rank, target, action, results):
        """Generate different dmg command related to OSA.
            Args:
            puuid (int) : Pool UUID
            rank (int)  : Daos Server Rank
            target (list) : Target list
            action (string) : String to identify OSA action
                              like drain, reintegration, extend
            results (queue) : dmg command output queue.
        """
        # dmg = DmgCommand(os.path.join(self.prefix, "bin"))
        dmg = copy.copy(self.dmg_command)
        self.log.info("Action: {0}".format(action))
        try:
            if action == "exclude":
                dmg.pool_exclude(puuid, (rank + 1), target)
            elif action == "drain":
                dmg.pool_drain(puuid, rank)
            elif action == "reintegrate":
                time.sleep(60)
                dmg.pool_reintegrate(puuid, (rank + 1), target)
            else:
                self.fail("Invalid action for dmg thread")
        except CommandFailure as _error:
            results.put("{} failed".format(action))
            # elif action == "extend":
            #    dmg.pool_extend(puuid, (rank + 2))

    def run_offline_parallel_test(self, num_pool, data=False):
        """Run multiple OSA commands in parallel with or without data.
            Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
        """
        # Create a pool
        pool = {}
        pool_uuid = []
        target_list = []

        # Exclude target : random two targets (target idx : 0-7)
        n = random.randint(0, 6)
        target_list.append(n)
        target_list.append(n+1)
        t_string = "{},{}".format(target_list[0], target_list[1])

        # Exclude rank 2.
        rank = 2

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

        # Exclude and reintegrate the pool_uuid, rank and targets
        for val in range(0, num_pool):
            self.pool = pool[val]
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            # Create the threads here
            threads = []
            osa_tasks = ["drain", "exclude", "reintegrate"]
            i = 0
            for action in osa_tasks:
                # Add a dmg thread
                process = threading.Thread(target=self.dmg_thread,
                                           kwargs={"puuid":
                                                   self.pool.uuid,
                                                   "rank": rank,
                                                   "target": t_string,
                                                   "action": action,
                                                   "results":
                                                   self.out_queue})
                process.start()
                threads.append(process)
                i = i + 1

        # Wait to finish the threads
        for thrd in threads:
            thrd.join()
            time.sleep(5)

        # Check the queue for any failure.
        tmp_list = list(self.out_queue.queue)
        for failure in tmp_list:
            if "FAIL" in failure:
                self.fail("Test failed : {0}".format(failure))

        if data:
            self.verify_single_object()

        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            pool[val].display_pool_daos_space(display_string)
            pver_end = self.get_pool_version()
            self.log.info("Pool Version at the End %s", pver_end)
            self.assertTrue(pver_end == 25,
                            "Pool Version Error:  at the end")
            pool[val].destroy()

    def test_osa_offline_parallel_test(self):
        """
        JIRA ID: DAOS-4752

        Test Description: Runs multiple OSA commands in parallel.

        :avocado: tags=all,pr,hw,large,osa,osa_parallel,offline_parallel
        """
        # Perform drain testing with 1 to 2 pools
        # Two pool testing blocked by DAOS-5333.
        # Fix range from 1,2 to 1,3
        self.run_offline_parallel_test(1)
        # Perform drain testing : inserting data in pool
        # Bug : DAOS-4946 blocks the following test case.
        self.run_offline_parallel_test(1, True)
