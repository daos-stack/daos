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
import uuid
import threading

from itertools import product
from avocado import fail_on
from apricot import TestWithServers
from test_utils_pool import TestPool
from ior_utils import IorCommand
from job_manager_utils import Mpirun
from write_host_file import write_host_file
from command_utils import CommandFailure
from mpio_utils import MpioUtils

try:
    # python 3.x
    import queue as queue
except ImportError:
    # python 2.7
    import Queue as queue


class OSAOnlineDrain(TestWithServers):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server Online Drain test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(OSAOnlineDrain, self).setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_test_sequence = self.params.get("ior_test_sequence",
                                                 '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get("obj_class",
                                               '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.out_queue = queue.Queue()

    @fail_on(CommandFailure)
    def get_pool_leader(self):
        """Get the pool leader
           Returns :
            int : pool leader value
        """
        out = []
        kwargs = {"pool": self.pool.uuid}
        out = self.dmg_command.get_output("pool_query", **kwargs)
        return int(out[0][3])

    @fail_on(CommandFailure)
    def get_pool_version(self):
        """Get the pool version
           Returns :
            int : pool version value
        """
        out = []
        kwargs = {"pool": self.pool.uuid}
        out = self.dmg_command.get_output("pool_query", **kwargs)
        return int(out[0][4])

    def ior_thread(self, pool, oclass, api, test, flags, results):
        """Start threads and wait until all threads are finished.
        Args:
            pool (object): pool handle
            oclass (str): IOR object class
            API (str): IOR API
            test (list): IOR test sequence
            flags (str): IOR flags
            results (queue): queue for returning thread results
        """
        processes = self.params.get("slots", "/run/ior/clientslots/*")
        container_info = {}
        mpio_util = MpioUtils()
        if mpio_util.mpich_installed(self.hostlist_clients) is False:
            self.fail("Exiting Test: Mpich not installed")
        self.pool = pool
        # Define the arguments for the ior_runner_thread method
        ior_cmd = IorCommand()
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(self.server_group, self.pool)
        ior_cmd.dfs_oclass.update(oclass)
        ior_cmd.api.update(api)
        ior_cmd.transfer_size.update(test[2])
        ior_cmd.block_size.update(test[3])
        ior_cmd.flags.update(flags)

        container_info["{}{}{}"
                       .format(oclass,
                               api,
                               test[2])] = str(uuid.uuid4())

        # Define the job manager for the IOR command
        manager = Mpirun(ior_cmd, mpitype="mpich")
        manager.job.daos_cont.update(container_info
                                     ["{}{}{}".format(oclass,
                                                      api,
                                                      test[2])])
        env = ior_cmd.get_default_env(str(manager))
        manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        manager.assign_processes(processes)
        manager.assign_environment(env, True)

        # run IOR Command
        try:
            manager.run()
        except CommandFailure as _error:
            results.put("FAIL")

    def run_online_drain_test(self, num_pool):
        """Run the Online drain without data.
            Args:
             int : total pools to create for testing purposes.
        """
        num_jobs = self.params.get("no_parallel_job", '/run/ior/*')
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

        # Drain one of the ranks (or server)
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

        # Drain the pool_uuid, rank and targets
        for val in range(0, num_pool):
            for oclass, api, test, flags in product(self.ior_dfs_oclass,
                                                    self.ior_apis,
                                                    self.ior_test_sequence,
                                                    self.ior_flags):
                threads = []
                for thrd in range(0, num_jobs):
                    # Add a thread for these IOR arguments
                    threads.append(threading.Thread(target=self.ior_thread,
                                                    kwargs={"pool": pool[val],
                                                            "oclass": oclass,
                                                            "api": api,
                                                            "test": test,
                                                            "flags": flags,
                                                            "results":
                                                            self.out_queue}))
                # Launch the IOR threads
                for thrd in threads:
                    self.log.info("Thread : %s", thrd)
                    thrd.start()
                    time.sleep(5)
            self.pool = pool[val]
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            output = self.dmg_command.pool_drain(self.pool.uuid,
                                                 rank, t_string)
            self.log.info(output)

            fail_count = 0
            while fail_count <= 20:
                pver_drain = self.get_pool_version()
                time.sleep(10)
                fail_count += 1
                if pver_drain > pver_begin + 1:
                    break

            self.log.info("Pool Version after drain %s", pver_drain)
            # Check pool version incremented after pool exclude
            self.assertTrue(pver_drain > pver_begin,
                            "Pool Version Error:  After drain")
            # Wait to finish the threads
            for thrd in threads:
                thrd.join()

        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            self.pool = pool[val]
            self.pool.display_pool_daos_space(display_string)
            pool[val].destroy()

    def test_osa_online_drain(self):
        """Test ID: DAOS-4750
        Test Description: Validate Online drain

        :avocado: tags=all,pr,hw,large,osa,osa_drain,online_drain
        """
        # Perform drain testing with 1 to 2 pools
        for pool_num in range(1, 3):
            self.run_online_drain_test(pool_num)
