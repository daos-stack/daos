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
from test_utils_container import TestContainer
from ior_utils import IorCommand
from job_manager_utils import Mpirun
from command_utils_base import CommandFailure
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

    def ior_runner_thread(self, test_case, pool_array, num_jobs, results):
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
        pool_len = len(pool_array)
        for pool in pool_array:
            self.pool = pool
            for oclass, api, test, flags in product(self.ior_daos_oclass,
                                                    self.ior_apis,
                                                    self.ior_transfer_size,
                                                    self.ior_flags):
                # Define the arguments for the ior_runner_thread method
                ior_cmd = IorCommand()
                ior_cmd.get_params(self)
                ior_cmd.set_daos_params(self.server_group, self.pool)
                ior_cmd.daos_oclass.update(oclass)
                ior_cmd.api.update(api)
                ior_cmd.transfer_size.update(test[0])
                # For test case 1, reduce the block size so that
                # IOR aggregation file size < NVME size.
                if test_case == 1:
                    test[1] = test[1] / 4
                # Update the block size based on no of jobs.
                actual_block_size = (round(float(test[1])) /
                                     (num_jobs * pool_len))
                ior_cmd.block_size.update(str(actual_block_size))
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
                    results.put("FAIL")

    def test_create_delete(self, num_pool=2, num_cont=5, total_count=100):
        """
        Test Description:
            This method is called with
            num_pool parameter to run following test case
            scenario's.
            Use Cases
             1. Create Pool/Container and destroy them. Check Space.
        """
        pool = {}
        cont = {}

        for loop_count in range(0, total_count):
            self.log.info("Running test %s", loop_count)
            for val in range(0, num_pool):
                pool[val] = TestPool(self.context,
                                     dmg_command=self.get_dmg_command())
                pool[val].get_params(self)
                # Split total SCM and NVME size for creating multiple pools.
                split_pool_scm_size = pool[val].scm_size.value / num_pool
                split_pool_nvme_size = pool[val].nvme_size.value / num_pool
                pool[val].scm_size.update(split_pool_scm_size)
                pool[val].nvme_size.update(split_pool_nvme_size)
                pool[val].create()
                display_string = "pool{} space at the Beginning".format(val)
                pool[val].display_pool_daos_space(display_string)
                for cont_val in range(0, num_cont):
                    cont[cont_val] = TestContainer(pool[val])

            for val in range(0, num_pool):
                pool[val].destroy()
                display_string = "Pool{} space at the End".format(val)
                pool[val].display_pool_daos_space(display_string)

    def test_run(self, test_case, num_pool=1, full_ssd_test=0):
        """
        Test Description:
            This method is called with different test_case,
            num_pool parameter to run differe test case
            scenario's.
            Use Cases
        """
        no_of_jobs = self.params.get("no_parallel_job", '/run/ior/*')
        # Create a pool
        pools = []
        pool = {}

        for val in range(0, num_pool):
            pool[val] = TestPool(self.context,
                                 dmg_command=self.get_dmg_command())
            pool[val].get_params(self)
            # For test case 1, use only half disk space.
            if (test_case == 1) and (full_ssd_test == 0):
                pool[val].scm_size.value = pool[val].scm_size.value / 2
                pool[val].nvme_size.value = pool[val].nvme_size.value / 2
            # Split the total SCM and NVME size for creating multiple pools.
            split_pool_scm_size = pool[val].scm_size.value / num_pool
            split_pool_nvme_size = pool[val].nvme_size.value / num_pool
            pool[val].scm_size.update(split_pool_scm_size)
            pool[val].nvme_size.update(split_pool_nvme_size)
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
                                                kwargs={"test_case": test_case,
                                                        "pool_array": pools,
                                                        "num_jobs": no_of_jobs,
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
            # For 400G/800G, test should fail with ENOSPC.
            while not self.out_queue.empty():
                if (self.out_queue.get()) == "FAIL" and \
                   (test_case != 2):
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
            Use Cases
            Test Case 1:
             1. Perform IO less than entire SSD disk space.
            Test Case 2:
             2. Perform IO beyond entire SSD disk space.
            Test Case 3:
             3. Create Pool/Container and destroy them several times.
            Test Case 4:
             4. Multiple Pool/Container testing.

        Use case:
        :avocado: tags=all,hw,large,nvme,pr
        :avocado: tags=nvme_pool_capacity
        """
        # Test Case 1 with one pool.
        self.log.info("Running Test Case 1 with one Pool")
        self.test_run(1)
        # Test Case 1 with two pools, check full SSD.
        self.log.info("Running Test Case 1 with two Pools")
        self.test_run(1, 2, 1)
        # Test Case 2 : with 2 pools
        self.log.info("Running Test Case 2 with two Pools")
        self.test_run(2, 2)
        # Test Case 3: create/delete pool/container
        self.log.info("Running Test Case 3: Pool/Cont Create/Destroy")
        self.test_create_delete()
        self.test_create_delete(10, 50, 100)
