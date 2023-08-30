"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from threading import Thread
from itertools import product
import queue

from apricot import TestWithServers
from write_host_file import write_host_file
from ior_utils import IorCommand
from exception_utils import CommandFailure
from job_manager_utils import get_job_manager


class NvmeFragmentation(TestWithServers):
    # pylint: disable=too-many-instance-attributes
    """NVMe drive fragmentation test cases.

    Test class Description:
        Verify the drive fragmentation does free the space and do not lead to
        ENOM_SPACE.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()

        self.ior_flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_transfer_size = self.params.get("transfer_block_size", '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get("obj_class", '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.out_queue = queue.Queue()

    def ior_runner_thread(self, results):
        """Start threads and wait until all threads are finished.

        Destroy the container at the end of this thread run.

        Args:
            results (queue): queue for returning thread results

        """
        processes = self.params.get("slots", "/run/ior/clientslots/*")
        cont_list = []
        daos_cmd = self.get_daos_command()

        # Iterate through IOR different value and run in sequence
        for oclass, api, test, flags in product(self.ior_dfs_oclass,
                                                self.ior_apis,
                                                self.ior_transfer_size,
                                                self.ior_flags):
            # Define the arguments for the ior_runner_thread method
            ior_cmd = IorCommand()
            ior_cmd.get_params(self)
            cont_label = self.label_generator.get_label('cont')
            ior_cmd.set_daos_params(self.server_group, self.pool, cont_label)
            ior_cmd.dfs_oclass.update(oclass)
            ior_cmd.api.update(api)
            ior_cmd.transfer_size.update(test[0])
            ior_cmd.block_size.update(test[1])
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
                cont_list.append(cont_label)
            except CommandFailure as error:
                results.put("FAIL - {}".format(error))

        # Destroy the container created by thread
        for cont_label in cont_list:
            try:
                daos_cmd.container_destroy(pool=self.pool.identifier, cont=cont_label)
            except CommandFailure as error:
                results.put("FAIL - {}".format(error))

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

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme
        :avocado: tags=NvmeFragmentation,test_nvme_fragmentation
        """
        num_repeat = self.params.get("num_repeat", '/run/ior/*')
        num_parallel_job = self.params.get("num_parallel_job", '/run/ior/*')
        # Create a pool
        self.add_pool(connect=False)
        self.pool.display_pool_daos_space("Pool space at the Beginning")

        # Repeat the test many times
        for test_loop in range(num_repeat):
            self.log.info("--Test Repeat for loop %s---", test_loop)
            # Create the IOR threads
            threads = []
            for _ in range(num_parallel_job):
                # Add a thread for these IOR arguments
                threads.append(
                    Thread(target=self.ior_runner_thread, kwargs={"results": self.out_queue}))
            # Launch the IOR threads
            for thrd in threads:
                thrd.start()
                time.sleep(5)
            # Wait to finish the threads
            for thrd in threads:
                thrd.join()

            # Verify the queue and make sure no FAIL for any IOR run
            while not self.out_queue.empty():
                msg = self.out_queue.get()
                if 'FAIL' in msg:
                    self.fail(msg)

        self.pool.display_pool_daos_space("Pool space at the End")
