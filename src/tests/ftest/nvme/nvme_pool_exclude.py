#!/usr/bin/python3
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import random
import threading

from test_utils_pool import TestPool, LabelGenerator
from osa_utils import OSAUtils
from write_host_file import write_host_file


class NvmePoolExclude(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    NVME Pool Exclude test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.daos_command = self.get_daos_command()
        self.ior_test_sequence = self.params.get("ior_test_sequence",
                                                 '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.cont_list = []
        self.dmg_command.exit_status_exception = True

    def run_nvme_pool_exclude(self, num_pool, oclass=None):
        """This is the main method which performs the actual
        testing. It does the following jobs:
        - Create number of TestPools
        - Start the IOR threads for running on each pools.
        - On each pool do the following:
            - Perform an IOR write (using a container)
            - Exclude a daos_server
            - Perform an IOR read/verify (same container used for write)
        Args:
            num_pool (int) : total pools to create for testing purposes.
            oclass (str) : object class (eg: RP_2G8, S1,etc).
                           Defaults to None
        """
        # Create a pool
        label_generator = LabelGenerator()
        pool = {}
        target_list = []

        # Exclude target : random two targets (target idx : 0-7)
        n = random.randint(0, 6)
        target_list.append(n)
        target_list.append(n+1)
        t_string = "{},{}".format(target_list[0], target_list[1])
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value

        # Exclude rank :  ranks other than rank 0.
        exclude_servers = len(self.hostlist_servers) * 2
        rank_list = list(range(1, exclude_servers))

        for val in range(0, num_pool):
            pool[val] = TestPool(
                context=self.context, dmg_command=self.dmg_command,
                label_generator=label_generator)
            pool[val].get_params(self)
            pool[val].create()
            pool[val].set_property("reclaim", "disabled")

        for val in range(0, num_pool):
            self.pool = pool[val]
            self.add_container(self.pool)
            self.cont_list.append(self.container)
            for test in self.ior_test_sequence:
                threads = []
                threads.append(threading.Thread(target=self.run_ior_thread,
                                                kwargs={"action": "Write",
                                                        "oclass": oclass,
                                                        "test": test}))
                # Launch the IOR threads
                for thrd in threads:
                    self.log.info("Thread : %s", thrd)
                    thrd.start()
                    time.sleep(1)

                self.pool.display_pool_daos_space("Pool space: Before Exclude")
                pver_begin = self.get_pool_version()

                index = random.randint(1, len(rank_list))
                rank = rank_list.pop(index-1)
                self.log.info("Removing rank %d", rank)

                self.log.info("Pool Version at the beginning %s", pver_begin)
                output = self.dmg_command.pool_exclude(self.pool.uuid,
                                                       rank, t_string)
                self.print_and_assert_on_rebuild_failure(output)

                pver_exclude = self.get_pool_version()
                self.log.info("Pool Version after exclude %s", pver_exclude)
                # Check pool version incremented after pool exclude
                self.assertTrue(pver_exclude > pver_begin,
                                "Pool Version Error:  After exclude")
                # Wait to finish the threads
                for thrd in threads:
                    thrd.join()
                    if not self.out_queue.empty():
                        self.assert_on_exception()
                # Verify the data after pool exclude
                self.run_ior_thread("Read", oclass, test)
                display_string = "Pool{} space at the End".format(val)
                self.pool.display_pool_daos_space(display_string)
                kwargs = {"pool": self.pool.uuid,
                          "cont": self.container.uuid}
                output = self.daos_command.container_check(**kwargs)
                self.log.info(output)

    def test_nvme_pool_excluded(self):
        """Test ID: DAOS-2086
        Test Description: This method is called from
        the avocado test infrastructure. This method invokes
        NVME pool exclude testing on multiple pools.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=nvme,checksum,nvme_osa
        :avocado: tags=nvme_pool_exclude
        """
        self.run_nvme_pool_exclude(1)
