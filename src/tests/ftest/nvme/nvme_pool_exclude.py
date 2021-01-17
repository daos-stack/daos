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
import uuid
import random
import threading

from itertools import product
from avocado import fail_on
from apricot import TestWithServers, skipForTicket
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


class NvmePoolExclude(TestWithServers):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    NVME Pool Exclude test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(NvmePoolExclude, self).setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_w_flags = self.params.get("write_flags", '/run/ior/iorflags/*')
        self.ior_r_flags = self.params.get("read_flags", '/run/ior/iorflags/*')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_test_sequence = self.params.get("ior_test_sequence",
                                                 '/run/ior/iorflags/*')
        self.ior_daos_oclass = self.params.get("obj_class",
                                               '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get(
            "obj_class", '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.out_queue = queue.Queue()
        self.container_info = {}

    @fail_on(CommandFailure)
    def get_rebuild_status(self):
        """Get the rebuild status.

        Returns:
            str: reuild status

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return data["rebuild"]["status"]

    @fail_on(CommandFailure)
    def get_pool_version(self):
        """Get the pool version.

        Returns:
            int: pool_version_value

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return int(data["version"])

    def ior_thread(self, pool, oclass, api, test, flags, results):
        """This method calls job manager for IOR command
        invocation.
        Args:
            pool (object): pool handle
            oclass (str): IOR object class
            API (str): IOR API
            test (list): IOR test sequence
            flags (str): IOR flags
            results (queue): queue for returning thread results
        """
        processes = self.params.get("slots", "/run/ior/clientslots/*")
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
        ior_cmd.transfer_size.update(test[0])
        ior_cmd.block_size.update(test[1])
        ior_cmd.flags.update(flags)
        if "-w" in flags:
            self.container_info["{}{}{}"
                                .format(oclass,
                                        api,
                                        test[0])] = str(uuid.uuid4())

        # Define the job manager for the IOR command
        manager = Mpirun(ior_cmd, mpitype="mpich")
        key = "".join([oclass, api, str(test[0])])
        manager.job.dfs_cont.update(self.container_info[key])
        env = ior_cmd.get_default_env(str(manager))
        manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        manager.assign_processes(processes)
        manager.assign_environment(env, True)

        # run IOR Command
        try:
            manager.run()
        except CommandFailure as _error:
            results.put("FAIL")

    def run_ior_thread(self, action, oclass, api, test):
        """ This method calls ior_thread method to generate
        IOR command and starts the thread.
        Args:
            action (str): Start the IOR thread with Read or
                          Write
            oclass (str): IOR object class
            API (str): IOR API
            test (list): IOR test sequence
            flags (str): IOR flags
        """
        if action == "Write":
            flags = self.ior_w_flags
        else:
            flags = self.ior_r_flags

        # Add a thread for these IOR arguments
        process = threading.Thread(target=self.ior_thread,
                                   kwargs={"pool": self.pool,
                                           "oclass": oclass,
                                           "api": api,
                                           "test": test,
                                           "flags": flags,
                                           "results":
                                           self.out_queue})
        # Launch the IOR thread
        process.start()
        # Wait for the thread to finish
        process.join()

    def run_nvme_pool_exclude(self, num_pool):
        """This is the main method which performs the actual
        testing. It does the following jobs:
        - Create number of TestPools
        - Start the IOR threads for running on each pools.
        - On each pool do the following:
            - Perform an IOR write (using a container)
            - Exclude a daos_server
            - Perform an IOR read/verify (same container used for write)
        Args:
            int : total pools to create for testing purposes.
        """
        # Create a pool
        pool = {}
        target_list = []

        # Exclude target : random two targets (target idx : 0-7)
        n = random.randint(0, 6)
        target_list.append(n)
        target_list.append(n+1)
        t_string = "{},{}".format(target_list[0], target_list[1])

        # Exclude rank :  ranks other than rank 0.
        exclude_servers = len(self.hostlist_servers) * 2
        rank_list = list(range(1, exclude_servers))

        for val in range(0, num_pool):
            pool[val] = TestPool(self.context, dmg_command=self.dmg_command)
            pool[val].get_params(self)
            # Split total SCM and NVME size for creating multiple pools.
            pool[val].scm_size.value = int(pool[val].scm_size.value /
                                           num_pool)
            pool[val].nvme_size.value = int(pool[val].nvme_size.value /
                                            num_pool)
            pool[val].create()

        for val in range(0, num_pool):
            self.pool = pool[val]
            for oclass, api, test in product(self.ior_dfs_oclass,
                                             self.ior_apis,
                                             self.ior_test_sequence):
                self.run_ior_thread("Write", oclass, api, test)

                self.pool.display_pool_daos_space("Pool space: Before Exclude")
                pver_begin = self.get_pool_version()

                index = random.randint(1, len(rank_list))
                rank = rank_list.pop(index-1)
                self.log.info("Removing rank %d", rank)

                time.sleep(5)
                self.log.info("Pool Version at the beginning %s", pver_begin)
                output = self.dmg_command.pool_exclude(self.pool.uuid,
                                                       rank, t_string)
                self.log.info(output)
                fail_count = 0
                while fail_count <= 20:
                    rebuild_status = self.get_rebuild_status()
                    time.sleep(15)
                    fail_count += 1
                    if rebuild_status == "done":
                        break

                self.assertTrue(fail_count <= 20, "Rebuild Not Completed")
                pver_exclude = self.get_pool_version()
                self.log.info("Pool Version after exclude %s", pver_exclude)
                # Check pool version incremented after pool exclude
                self.assertTrue(pver_exclude > pver_begin,
                                "Pool Version Error:  After exclude")

                # Verify the data after pool exclude
                self.run_ior_thread("Read", oclass, api, test)

        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            self.pool = pool[val]
            self.pool.display_pool_daos_space(display_string)
            pool[val].destroy()

    @skipForTicket("DAOS-6108")
    def test_nvme_pool_excluded(self):
        """Test ID: DAOS-2086
        Test Description: This method is called from
        the avocado test infrastructure. This method invokes
        NVME pool exclude testing on multiple pools.

        :avocado: tags=all,full_regression,hw,large,nvme
        :avocado: tags=nvme_pool_exclude
        """
        for num_pool in range(1, 4):
            self.run_nvme_pool_exclude(num_pool)
