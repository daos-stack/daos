#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
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
import queue


class NvmePoolExtend(TestWithServers):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    NVME Pool Extend test cases.
    - Start the few daos servers.
    - Create a pool
    - Run IOR with write mode
    - Start a new server and extend the pool
    - Verify IOR written data after extending the pool.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
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
    def get_pool_version(self):
        """Get the pool version.

        Returns:
            int: pool_version_value

        """
        data = self.dmg_command.pool_query(self.pool.uuid)
        return int(data["response"]["version"])

    def ior_thread(self, pool, oclass, api, test, flags, results):
        """Start threads and wait until all threads are finished.
        Args:
            pool (object): pool
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
        # Define the parameters for the ior_runner_thread method
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
        """Start the IOR thread for either writing or
        reading data to/from a container.
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

    def create_pool(self):
        """ Creates a TestPool based on yaml params.

        Returns:
            TestPool: Created pool
        """
        # Create a pool
        pool = TestPool(self.context, dmg_command=self.dmg_command)
        pool.get_params(self)
        pool.create()
        return pool

    def run_nvme_pool_extend(self):
        """Run Pool Extend
        """
        total_servers = len(self.hostlist_servers) * 2
        self.log.info("Total Daos Servers (Initial): %d", total_servers)

        # The last two available ranks will be used to
        # extend the created pools
        initial_extra_ranks = [total_servers - 2, total_servers - 1]
        # String version to use within dmg
        extra_ranks = ",".join(map(str, initial_extra_ranks))
        pver_extend = 0

        for oclass, api, test in product(self.ior_dfs_oclass,
                                         self.ior_apis,
                                         self.ior_test_sequence):

            self.pool = self.create_pool()
            scm_pool_size = self.pool.scm_size
            nvme_pool_size = self.pool.nvme_size

            self.run_ior_thread("Write", oclass, api, test)
            self.pool.display_pool_daos_space("Pool space: Beginning")

            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)

            # Extend pull, this should modify the pool version
            output = self.dmg_command.pool_extend(self.pool.uuid,
                                                  extra_ranks, scm_pool_size,
                                                  nvme_pool_size)
            self.log.info(output)
            # Wait for rebuild to complete
            self.pool.wait_for_rebuild(to_start=False)
            pver_extend = self.get_pool_version()

            self.log.info("Pool Version after extend %s", pver_extend)
            # Check pool version incremented after pool extend
            self.assertTrue(pver_extend > pver_begin,
                            "Pool Version Error:  After extend")

            # Verify the data after pool extend
            self.run_ior_thread("Read", oclass, api, test)
            # Get the pool space at the end of the test
            display_string = "Pool:{} space at the End".format(self.pool.uuid)
            self.pool.display_pool_daos_space(display_string)

            # Cleanup
            self.pool.destroy()


    def test_nvme_pool_extend(self):
        """Test ID: DAOS-2086
        Test Description: NVME Pool Extend

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme,checksum
        :avocado: tags=nvme_pool_extend
        """
        self.run_nvme_pool_extend()
