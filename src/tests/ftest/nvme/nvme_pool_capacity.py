#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import threading
import uuid
from itertools import product

from apricot import TestWithServers
from write_host_file import write_host_file
from test_utils_container import TestContainer
from ior_utils import IorCommand
from job_manager_utils import Mpirun
from command_utils_base import CommandFailure
from mpio_utils import MpioUtils
import queue


class NvmePoolCapacity(TestWithServers):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify NOSPC
    condition is reported when accessing data beyond
    pool size.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()

        self.ior_flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_test_sequence = self.params.get(
            "ior_test_sequence", '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get(
            "obj_class", '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.out_queue = queue.Queue()

    def ior_thread(self, pool, oclass, api, test, flags, results):
        """Start threads and wait until all threads are finished.

        Args:
            pool (TestPool): Pool to run IOR command on.
            oclass (str): IOR object class
            API (str): IOR API
            test (list): IOR test sequence
            flags (str): IOR flags
            results (queue): queue for returning thread results

        Returns:
            None

        """
        processes = self.params.get("slots", "/run/ior/clientslots/*")
        container_info = {}
        mpio_util = MpioUtils()
        if mpio_util.mpich_installed(self.hostlist_clients) is False:
            self.fail("Exiting Test: Mpich not installed")

        # Define the arguments for the ior_runner_thread method
        ior_cmd = IorCommand()
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(self.server_group, pool)
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
        self.job_manager = Mpirun(ior_cmd, mpitype="mpich")
        key = "{}{}{}".format(oclass, api, test[2])
        self.job_manager.job.dfs_cont.update(container_info[key])
        env = ior_cmd.get_default_env(str(self.job_manager))
        self.job_manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        self.job_manager.assign_processes(processes)
        self.job_manager.assign_environment(env, True)

        # run IOR Command
        try:
            self.job_manager.run()
        except CommandFailure as _error:
            results.put("FAIL")

    def test_create_delete(self, num_pool=2, num_cont=5, total_count=100,
                           scm_size=100000000000, nvme_size=300000000000):
        """
        Test Description:
            This method is used to create/delete pools
            for a long run. It verifies the NVME free space
            during this process.
            Args:
                num_pool (int): Total pools for running test
                num_cont (int): Total containers created on each pool
                total_count (int): Total times the test is run in a loop
                scm_size (int): SCM size used in the testing
                nvme_size (int): NVME size used in the testing
            Returns:
                None
        """
        self.pool = []
        cont = {}

        for loop_count in range(0, total_count):
            self.log.info("Running test %s", loop_count)
            for val in range(0, num_pool):
                self.pool.append(self.get_pool(create=False))
                # Split total SCM and NVME size for creating multiple pools.
                temp = int(scm_size) / num_pool
                self.pool[-1].scm_size.update(str(temp))
                temp = int(nvme_size) / num_pool
                self.pool[-1].nvme_size.update(str(temp))
                self.pool[-1].create()

                display_string = "pool{} space at the Beginning".format(val)
                self.pool[-1].display_pool_daos_space(display_string)
                nvme_size_begin = self.pool[-1].get_pool_free_space("NVME")
                for cont_val in range(0, num_cont):
                    cont[cont_val] = TestContainer(self.pool[-1])

            m_leak = 0

            # Destroy the last num_pool pools created
            offset = loop_count * num_pool
            for index in range(offset, offset + num_pool):
                display_string = "Pool {} space at the End".format(
                    self.pool[index].uuid)
                self.pool[index].display_pool_daos_space(display_string)
                nvme_size_end = self.pool[index].get_pool_free_space("NVME")
                self.pool[index].destroy()

                if (nvme_size_begin != nvme_size_end) and (m_leak == 0):
                    m_leak = val + 1

            # After destroying pools, check memory leak for each test loop.
            if m_leak != 0:
                self.fail("Memory leak : iteration {0} \n".format(m_leak))

    def test_run(self, num_pool=1):
        """
        Method Description:
            This method is called with different test_cases.
            Args:
               num_pool (int): Total pools for running a test.
            Returns:
               None
        """
        num_jobs = self.params.get("no_parallel_job", '/run/ior/*')
        # Create a pool
        self.pool = []
        loop_count = 0

        # Iterate through IOR different ior test sequence
        for oclass, api, test, flags in product(self.ior_dfs_oclass,
                                                self.ior_apis,
                                                self.ior_test_sequence,
                                                self.ior_flags):
            # Create the IOR threads
            threads = []
            for val in range(0, num_pool):
                self.pool.append(self.get_pool(create=False))
                # Split total SCM and NVME size for creating multiple pools.
                self.pool[-1].scm_size.value = int(test[0]) / num_pool
                self.pool[-1].nvme_size.value = int(test[1]) / num_pool
                self.pool[-1].create()
                display_string = "pool{} space at the Beginning".format(val)
                self.pool[-1].display_pool_daos_space(display_string)

                for thrd in range(0, num_jobs):
                    # Add a thread for these IOR arguments
                    threads.append(threading.Thread(target=self.ior_thread,
                                                    kwargs={
                                                        "pool": self.pool[-1],
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
            # Wait to finish the threads
            for thrd in threads:
                thrd.join()

            # Verify the queue and make sure no FAIL for any IOR run
            # Test should fail with ENOSPC.
            while not self.out_queue.empty():
                if (self.out_queue.get() == "FAIL" and test[4] == "PASS") \
                     or (self.out_queue.get() != "FAIL" and test[4] == "FAIL"):
                    self.fail("FAIL")

            # Destroy the last num_pool pools created
            offset = loop_count * num_pool
            for index in range(offset, offset + num_pool):
                display_string = "Pool {} space at the End".format(
                    self.pool[index].uuid)
                self.pool[index].display_pool_daos_space(display_string)
                self.pool[index].destroy()

            loop_count += 1

    def test_nvme_pool_capacity(self):
        """Jira ID: DAOS-2085.

        Test Description:
            Purpose of this test is to verify whether DAOS stack
            report NOSPC when accessing data beyond pool size.
            Use Cases
            Test Case 1 or 2:
             1. Perform IO less than entire SSD disk space.
             2. Perform IO beyond entire SSD disk space.
            Test Case 3:
             3. Create Pool/Container and destroy them several times.

        Use case:
        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=ib2,nvme,nvme_pool_capacity
        """
        # Run test with one pool.
        self.log.info("Running Test Case 1 with one Pool")
        self.test_run(1)
        time.sleep(5)
        # Run test with two pools.
        self.log.info("Running Test Case 1 with two Pools")
        self.test_run(2)
        time.sleep(5)
        # Run Create/delete pool/container
        self.log.info("Running Test Case 3: Pool/Cont Create/Destroy")
        self.test_create_delete(10, 50, 100)
