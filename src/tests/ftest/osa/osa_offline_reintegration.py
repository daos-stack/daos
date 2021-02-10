#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random
import threading
from osa_utils import OSAUtils
from test_utils_pool import TestPool
from write_host_file import write_host_file

try:
    # python 3.x
    import queue as queue
except ImportError:
    # python 2.7
    import Queue as queue

class OSAOfflineReintegration(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline reintegration test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(OSAOfflineReintegration, self).setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_w_flags = self.params.get("write_flags", '/run/ior/iorflags/*')
        self.ior_r_flags = self.params.get("read_flags", '/run/ior/iorflags/*')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_test_sequence = self.params.get(
            "ior_test_sequence", '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get(
            "obj_class", '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.out_queue = queue.Queue()

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


    def run_offline_reintegration_test(self, num_pool, data=False):
        """Run the offline reintegration without data.
            Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
        """
        # Create a pool
        pool = {}
        pool_uuid = []
        target_list = []
        exclude_servers = (len(self.hostlist_servers) * 2) - 1

        # Exclude target : random two targets (target idx : 0-7)
        n = random.randint(0, 6)
        target_list.append(n)
        target_list.append(n+1)
        t_string = "{},{}".format(target_list[0], target_list[1])

        # Exclude rank : two ranks other than rank 0.
        rank = random.randint(1, exclude_servers)

        for val in range(0, num_pool):
            pool[val] = TestPool(self.context,
                                 dmg_command=self.get_dmg_command())
            pool[val].get_params(self)
            # Split total SCM and NVME size for creating multiple pools.
            pool[val].scm_size.value = int(pool[val].scm_size.value /
                                           num_pool)
            pool[val].nvme_size.value = int(pool[val].nvme_size.value /
                                            num_pool)
            pool[val].create()
            pool_uuid.append(pool[val].uuid)
            self.pool = pool[val]
            if data:
                self.run_ior_thread("Write", self.ior_dfs_oclass[0],
                                    self.ior_apis[0], self.ior_test_sequence[0])

        # Exclude and reintegrate the pool_uuid, rank and targets
        for val in range(0, num_pool):
            self.pool = pool[val]
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            output = self.dmg_command.pool_exclude(self.pool.uuid,
                                                   rank)
            self.log.info(output)
            self.is_rebuild_done(3)
            self.assert_on_rebuild_failure()

            pver_exclude = self.get_pool_version()
            self.log.info("Pool Version after exclude %s", pver_exclude)
            # Check pool version incremented after pool exclude
            self.assertTrue(pver_exclude > (pver_begin + len(target_list)),
                            "Pool Version Error:  After exclude")
            output = self.dmg_command.pool_reintegrate(self.pool.uuid,
                                                       rank,
                                                       t_string)
            self.log.info(output)
            self.is_rebuild_done(3)
            self.assert_on_rebuild_failure()

            pver_reint = self.get_pool_version()
            self.log.info("Pool Version after reintegrate %d", pver_reint)
            # Check pool version incremented after pool reintegrate
            self.assertTrue(pver_reint > (pver_exclude + 1),
                            "Pool Version Error:  After reintegrate")

        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            self.pool = pool[val]
            self.pool.display_pool_daos_space(display_string)

        if data:
            self.run_ior_thread("Read", self.ior_dfs_oclass[0],
                                self.ior_apis[0], self.ior_test_sequence[0])

    def test_osa_offline_reintegration(self):
        """Test ID: DAOS-4749
        Test Description: Validate Offline Reintegration

        :avocado: tags=all,daily_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration
        """
        # Perform reintegration testing with a pool
        self.run_offline_reintegration_test(1, True)
