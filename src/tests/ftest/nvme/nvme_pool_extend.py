#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import threading

from test_utils_pool import TestPool
from osa_utils import OSAUtils
from write_host_file import write_host_file


class NvmePoolExtend(OSAUtils):
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
        self.ior_test_sequence = self.params.get("ior_test_sequence",
                                                 '/run/ior/iorflags/*')
        # Start an additional server.
        self.extra_servers = self.params.get("test_servers",
                                             "/run/extra_servers/*")
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.pool = None

    def run_nvme_pool_extend(self, num_pool, oclass=None):
        """Run Pool Extend
        Args:
            num_pool (int) : total pools to create for testing purposes.
            oclass (str) : object class (eg: RP_2G8,etc)
                           Defaults to None.
        """
        pool = {}
        total_servers = len(self.hostlist_servers) * 2
        self.log.info("Total Daos Servers (Initial): %d", total_servers)
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value

        for val in range(0, num_pool):
            # Create a pool
            pool[val] = TestPool(self.context, dmg_command=self.dmg_command)
            pool[val].get_params(self)
            pool[val].create()
            pool[val].set_property("reclaim", "disabled")

        for val in range(0, num_pool):
            threads = []
            self.pool = pool[val]
            for test in self.ior_test_sequence:
                threads.append(threading.Thread(target=self.run_ior_thread,
                                                kwargs={"action": "Write",
                                                        "oclass": oclass,
                                                        "test": test}))
                # Launch the IOR threads
                for thrd in threads:
                    self.log.info("Thread : %s", thrd)
                    thrd.start()
                    time.sleep(1)

                scm_pool_size = self.pool.scm_size
                nvme_pool_size = self.pool.nvme_size
                self.pool.display_pool_daos_space("Pool space: Beginning")
                pver_begin = self.get_pool_version()

                # Start the additional servers and extend the pool
                self.log.info("Extra Servers = %s", self.extra_servers)
                self.start_additional_servers(self.extra_servers)
                # Give some time for the additional server to come up.
                # Extending two ranks (two servers per node)
                time.sleep(25)
                self.log.info("Pool Version at the beginning %s", pver_begin)
                output = self.dmg_command.pool_extend(self.pool.uuid,
                                                      "6, 7", scm_pool_size,
                                                      nvme_pool_size)
                self.print_and_assert_on_rebuild_failure(output)
                pver_extend = self.get_pool_version()
                self.log.info("Pool Version after extend %s", pver_extend)
                # Check pool version incremented after pool extend
                self.assertTrue(pver_extend > pver_begin,
                                "Pool Version Error:  After extend")
                # Wait to finish the threads
                for thrd in threads:
                    thrd.join()
                # Verify the data after pool extend
                self.run_ior_thread("Read", oclass, test)
                # Get the pool space at the end of the test
                display_string = "Pool{} space at the End".format(val)
                self.pool = pool[val]
                self.pool.display_pool_daos_space(display_string)
                pool[val].destroy()

                # Stop the extra node servers (rank 6 and 7)
                output = self.dmg_command.system_stop(self.pool.uuid,
                                                      "6, 7")
                self.log.info(output)

    def test_nvme_pool_extend(self):
        """Test ID: DAOS-2086
        Test Description: NVME Pool Extend

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme,checksum,nvme_osa
        :avocado: tags=nvme_pool_extend
        """
        self.run_nvme_pool_extend(1)
