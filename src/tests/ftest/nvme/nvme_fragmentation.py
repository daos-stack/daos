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
import os
import threading
import uuid
from itertools import product

from apricot import TestWithServers
from write_host_file import write_host_file
from test_utils_pool import TestPool
from ior_utils import IorCommand
from daos_utils import DaosCommand
from command_utils_base import CommandFailure
from job_manager_utils import Mpirun
from mpio_utils import MpioUtils

try:
    # python 3.x
    import queue
except ImportError:
    # python 2.7
    import Queue as queue


class NvmeFragmentation(TestWithServers):
    # pylint: disable=too-many-ancestors
    """NVMe drive fragmentation test cases.

    Test class Description:
        Verify the drive fragmentation does free the space and do not lead to
        ENOM_SPACE.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super(NvmeFragmentation, self).setUp()

        self.ior_flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_transfer_size = self.params.get(
            "transfer_block_size", '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get(
            "obj_class", '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.out_queue = queue.Queue()

    def ior_runner_thread(self, results):
        """Start threads and wait until all threads are finished.

        Destroy the container at the end of this thread run.

        Args:
            results (queue): queue for returning thread results

        Returns:
            None

        """
        processes = self.params.get("slots", "/run/ior/clientslots/*")
        container_info = {}
        cmd = DaosCommand(os.path.join(self.prefix, "bin"))
        cmd.set_sub_command("container")
        cmd.sub_command_class.set_sub_command("destroy")
        mpio_util = MpioUtils()
        if mpio_util.mpich_installed(self.hostlist_clients) is False:
            self.fail("Exiting Test: Mpich not installed")

        # Iterate through IOR different value and run in sequence
        for oclass, api, test, flags in product(self.ior_dfs_oclass,
                                                self.ior_apis,
                                                self.ior_transfer_size,
                                                self.ior_flags):
            # Define the arguments for the ior_runner_thread method
            ior_cmd = IorCommand()
            ior_cmd.get_params(self)
            ior_cmd.set_daos_params(self.server_group, self.pool)
            ior_cmd.dfs_oclass.update(oclass)
            ior_cmd.api.update(api)
            ior_cmd.transfer_size.update(test[0])
            ior_cmd.block_size.update(test[1])
            ior_cmd.flags.update(flags)

            container_info["{}{}{}"
                           .format(oclass,
                                   api,
                                   test[0])] = str(uuid.uuid4())

            # Define the job manager for the IOR command
            self.job_manager = Mpirun(ior_cmd, mpitype="mpich")
            key = "{}{}{}".format(oclass, api, test[0])
            self.job_manager.job.dfs_cont.update(container_info[key])
            env = ior_cmd.get_default_env(str(self.job_manager))
            self.job_manager.assign_hosts(
                self.hostlist_clients, self.workdir, None)
            self.job_manager.assign_processes(processes)
            self.job_manager.assign_environment(env, True)

            # run IOR Command
            try:
                self.job_manager.run()
            except CommandFailure as _error:
                results.put("FAIL")

        # Destroy the container created by thread
        for key in container_info:
            cmd.sub_command_class.sub_command_class.pool.value = self.pool.uuid
            #cmd.sub_command_class.sub_command_class.svc.value = \
            #    self.pool.svc_ranks
            cmd.sub_command_class.sub_command_class.cont.value = \
                container_info[key]

            try:
                # pylint: disable=protected-access
                cmd._get_result()
            except CommandFailure as _error:
                results.put("FAIL")

    def test_nvme_fragmentation(self):
        """Jira ID: DAOS-2332.

        Test Description:
            Purpose of this test is to verify there is no Fragmentation
            after doing some IO write/delete operation for ~hour.

        Use case:
        Create object with different transfer size in parallel (10 IOR threads)
        Delete the container created by IOR which will dealloc NVMe block
        Run above code in loop for some time (~1 hours) and expect
        not to fail with NO ENOM SPAC.

        :avocado: tags=all,hw,medium,nvme,ib2,full_regression
        :avocado: tags=nvme_fragmentation
        """
        no_of_jobs = self.params.get("no_parallel_job", '/run/ior/*')
        # Create a pool
        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)
        self.pool.create()
        self.pool.display_pool_daos_space("Pool space at the Beginning")

        # Repeat the test for 30 times which will take ~1 hour
        for test_loop in range(30):
            self.log.info("--Test Repeat for loop %s---", test_loop)
            # Create the IOR threads
            threads = []
            for thrd in range(no_of_jobs):
                # Add a thread for these IOR arguments
                threads.append(threading.Thread(target=self.ior_runner_thread,
                                                kwargs={"results":
                                                        self.out_queue}))
            # Launch the IOR threads
            for thrd in threads:
                thrd.start()
                time.sleep(5)
            # Wait to finish the threads
            for thrd in threads:
                thrd.join()

            # Verify the queue and make sure no FAIL for any IOR run
            while not self.out_queue.empty():
                if self.out_queue.get() == "FAIL":
                    self.fail("FAIL")

        self.pool.display_pool_daos_space("Pool space at the End")
