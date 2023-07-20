"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import threading
from itertools import product
import queue

from apricot import TestWithServers
from write_host_file import write_host_file
from ior_utils import IorCommand
from job_manager_utils import get_job_manager
from exception_utils import CommandFailure


class NvmePoolCapacity(TestWithServers):
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
        self.ior_test_sequence = self.params.get("ior_test_sequence", '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get("obj_class", '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(self.hostlist_clients, self.workdir, None)
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

        """
        processes = self.params.get("slots", "/run/ior/clientslots/*")

        # Define the arguments for the ior_runner_thread method
        ior_cmd = IorCommand()
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(
            self.server_group, pool, self.label_generator.get_label('TestContainer'))
        ior_cmd.dfs_oclass.update(oclass)
        ior_cmd.api.update(api)
        ior_cmd.transfer_size.update(test[2])
        ior_cmd.block_size.update(test[3])
        ior_cmd.flags.update(flags)

        # Define the job manager for the IOR command
        job_manager = get_job_manager(self, job=ior_cmd)
        env = ior_cmd.get_default_env(str(job_manager))
        job_manager.assign_hosts(self.hostlist_clients, self.workdir, None)
        job_manager.assign_processes(processes)
        job_manager.assign_environment(env, True)

        # run IOR Command
        try:
            job_manager.run()
        except CommandFailure as error:
            results.put("FAIL - {}".format(str(error)))

    def run_test_create_delete(self, num_pool=2, num_cont=5, total_count=100):
        """
        Test Description:
            This method is used to create/delete pools
            for a long run. It verifies the NVME free space
            during this process.
            Args:
                num_pool (int): Total pools for running test
                num_cont (int): Total containers created on each pool
                total_count (int): Total times the test is run in a loop
            Returns:
                None
        """
        self.pool = []
        nvme_size_begin = {}
        nvme_size_end = {}

        for loop_count in range(0, total_count):
            self.log.info("Running test %s", loop_count)
            offset = loop_count * num_pool
            for val in range(offset, offset + num_pool):
                self.pool.append(self.get_pool(namespace="/run/pool_qty_{}/*".format(num_pool),
                                 properties="reclaim:disabled"))

                display_string = "pool{} space at the Beginning".format(val)
                self.pool[-1].display_pool_daos_space(display_string)
                nvme_size_begin[val] = self.pool[-1].get_pool_free_space("NVME")
                # To be fixed by DAOS-12974
                for _ in range(num_cont):
                    # self.get_container(self.pool[-1])
                    pass

            m_leak = 0

            # Destroy the last num_pool pools created
            for index in range(offset, offset + num_pool):
                display_string = "{} space at the End".format(str(self.pool[index]))
                self.pool[index].display_pool_daos_space(display_string)
                nvme_size_end[index] = self.pool[index].get_pool_free_space("NVME")
                self.pool[index].destroy()

                if (nvme_size_begin[index] != nvme_size_end[index]) and (m_leak == 0):
                    m_leak = m_leak + 1

            # After destroying pools, check memory leak for each test loop.
            if m_leak != 0:
                self.fail("Memory leak : iteration {}".format(m_leak))

    def run_test(self, num_pool=1):
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
                self.pool.append(self.get_pool(namespace="/run/pool_qty_{}/*".format(num_pool),
                                 properties="reclaim:disabled"))
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
                result = self.out_queue.get()
                if ("FAIL" in result and test[4] == "PASS"):
                    self.fail(result)

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
        :avocado: tags=nvme,pool
        :avocado: tags=NvmePoolCapacity,test_nvme_pool_capacity
        """
        # Run test with one pool.
        self.log.info("Running Test Case 1 with one Pool")
        self.run_test(1)
        time.sleep(5)
        # Run test with two pools.
        self.log.info("Running Test Case 1 with two Pools")
        self.run_test(2)
        time.sleep(5)
        # Run Create/delete pool/container
        self.log.info("Running Test Case 3: Pool/Cont Create/Destroy")
        self.run_test_create_delete(10, 50, 20)
