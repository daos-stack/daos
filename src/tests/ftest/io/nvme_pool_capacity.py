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
from command_utils import Mpirun, CommandFailure
from mpio_utils import MpioUtils

try:
    # python 3.x
    import queue as queue
except ImportError:
    # python 2.7
    import Queue as queue


class NvmePoolCapacity(TestWithServers):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify NOSPC
    condition is reported when accessing data beyond
    pool size.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super(NvmePoolCapacity, self).setUp()

        self.ior_flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_transfer_size = self.params.get("transfer_block_size",
                                                 '/run/ior/iorflags/*')
        self.ior_daos_oclass = self.params.get("obj_class",
                                               '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.out_queue = queue.Queue()

    def ior_runner_thread(self, test_case, pool_array, results):
        """Start threads and wait until all threads are finished.
        Destroy the container at the end of this thread run.

        Args:
            results (queue): queue for returning thread results

        Returns:
            None
        """
        processes = self.params.get("slots", "/run/ior/clientslots/*")
        container_info = {}
        mpio_util = MpioUtils()
        if mpio_util.mpich_installed(self.hostlist_clients) is False:
            self.fail("Exiting Test: Mpich not installed")

        # Iterate through IOR different value and run in sequence
        for pool in pool_array:
            self.pool = pool
            for oclass, api, test, flags in product(self.ior_daos_oclass,
                                                    self.ior_apis,
                                                    self.ior_transfer_size,
                                                    self.ior_flags):
                # Define the arguments for the ior_runner_thread method
                if (test_case < 4) and (test[1] == "32M"):
                    continue
                # Check for full NVMe size (360G, 800G: ENOSPC)
                if (test_case == 2) and (test[1] == "180G"):
                    test[1] = "360G"
                elif (test_case == 2) and (test[1] == "200G"):
                    test[1] = "800G"
                else:
                    self.log.info("Using block size : %s", test[1])
                ior_cmd = IorCommand()
                ior_cmd.get_params(self)
                ior_cmd.set_daos_params(self.server_group, self.pool)
                ior_cmd.daos_oclass.update(oclass)
                ior_cmd.api.update(api)
                ior_cmd.transfer_size.update(test[0])
                ior_cmd.block_size.update(test[1])
                ior_cmd.flags.update(flags)

                container_info["{}{}{}"
                               .format(oclass,
                                       api,
                                       test[0])] = str(uuid.uuid4())

                # Define the job manager for the IOR command
                manager = Mpirun(ior_cmd,
                                 os.path.join(mpio_util.mpichinstall, "bin"),
                                 mpitype="mpich")
                manager.job.daos_cont.update(container_info
                                             ["{}{}{}".format(oclass,
                                                              api,
                                                              test[0])])
                env = ior_cmd.get_default_env(str(manager))
                manager.setup_command(env, self.hostfile_clients,
                                      processes)

                # run IOR Command
                try:
                    manager.run()
                except CommandFailure as _error:
                    # If block_size 160G, it is expected to fail due to ENOSPC.
                    self.log.info("Block Size: %s", test[1])
                    if (test[1] == "200G") or (test[1] == "800G"):
                        results.put("PASS")
                    else:
                        results.put("FAIL")

    def test_caseA(self, num_pool=1):
        no_of_jobs = self.params.get("no_parallel_job", '/run/ior/*')
        # Create a pool
        pools = []
        pool = {}
        for val in range(0, num_pool):
            pool[val] = TestPool(self.context,
                                 dmg_command=self.get_dmg_command())
            pool[val].get_params(self)
            pool[val].create()
            display_string = "pool{} space at the Beginning".format(val)
            pool[val].display_pool_daos_space(display_string)
            pools.append(pool[val])

        self.log.info("Pools : %s", pools)

        for test_loop in range(1):
            self.log.info("--Test Repeat for loop %s---", test_loop)
            # Create the IOR threads
            threads = []
            for thrd in range(no_of_jobs):
                # Add a thread for these IOR arguments
                threads.append(threading.Thread(target=self.ior_runner_thread,
                                                kwargs={"pool_array": pools,
                                                        "results":
                                                        self.out_queue}))
            # Launch the IOR threads
            for thrd in threads:
                self.log.info("Thread : %s", thrd)
                thrd.start()
                time.sleep(5)
            # Wait to finish the threads
            for thrd in threads:
                thrd.join()

            # Verify the queue and make sure no FAIL for any IOR run
            while not self.out_queue.empty():
                if self.out_queue.get() == "FAIL":
                    self.fail("FAIL")
        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            pool[val].display_pool_daos_space(display_string)
            pool[val].destroy()

    def test_nvme_pool_capacity(self):
        """Jira ID: DAOS-2085.

        Test Description:
            Purpose of this test is to verify whether DAOS stack
            report NOSPC when accessing data beyond pool size.

        Use case:
        :avocado: tags=all,hw,medium,nvme,ib2,pr
        :avocado: tags=nvme_pool_capacity
        """
        self.test_caseA(1)
        self.test_caseA(2)
