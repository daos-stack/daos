#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import threading

from itertools import product
from apricot import skipForTicket
from test_utils_pool import TestPool
from write_host_file import write_host_file
from daos_racer_utils import DaosRacerCommand
from osa_utils import OSAUtils

try:
    # python 3.x
    import queue as queue
except ImportError:
    # python 2.7
    import Queue as queue


class OSAOnlineExtend(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server Online Extend test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(OSAOnlineExtend, self).setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_test_sequence = self.params.get("ior_test_sequence",
                                                 '/run/ior/iorflags/*')
        self.ior_daos_oclass = self.params.get("obj_class",
                                               '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get(
            "obj_class", '/run/ior/iorflags/*')
        # Start an additional server.
        self.extra_servers = self.params.get("test_servers",
                                             "/run/extra_servers/*")
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.out_queue = queue.Queue()
        self.ds_racer_queue = queue.Queue()
        self.daos_racer = None

    def daos_racer_thread(self):
        """Start the daos_racer thread."""
        self.daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients[0],
                                           self.dmg_command)
        self.daos_racer.get_params(self)
        self.daos_racer.set_environment(
            self.daos_racer.get_environment(self.server_managers[0]))
        self.daos_racer.run()

    def run_online_extend_test(self, num_pool, racer=False):
        """Run the Online extend without data.
            Args:
             int : total pools to create for testing purposes.
        """
        num_jobs = self.params.get("no_parallel_job", '/run/ior/*')
        # Create a pool
        pool = {}
        pool_uuid = []

        # Extend one of the ranks 4 and 5
        rank = [4, 5]

        # Start the daos_racer thread
        if racer is True:
            daos_racer_thread = threading.Thread(target=self.daos_racer_thread)
            daos_racer_thread.start()
            time.sleep(30)

        for val in range(0, num_pool):
            pool[val] = TestPool(self.context, self.get_dmg_command())
            pool[val].get_params(self)
            # Split total SCM and NVME size for creating multiple pools.
            pool[val].scm_size.value = int(pool[val].scm_size.value /
                                           num_pool)
            pool[val].nvme_size.value = int(pool[val].nvme_size.value /
                                            num_pool)
            pool[val].create()
            pool_uuid.append(pool[val].uuid)

        # Extend the pool_uuid, rank and targets
        for val in range(0, num_pool):
            threads = []
            for oclass, api, test, flags in product(self.ior_dfs_oclass,
                                                    self.ior_apis,
                                                    self.ior_test_sequence,
                                                    self.ior_flags):
                for _ in range(0, num_jobs):
                    # Add a thread for these IOR arguments
                    threads.append(threading.Thread(target=self.ior_thread,
                                                    kwargs={"pool": pool[val],
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
                    time.sleep(1)
            self.pool = pool[val]
            scm_size = self.pool.scm_size
            nvme_size = self.pool.nvme_size
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()

            # Start the additional servers and extend the pool
            self.log.info("Extra Servers = %s", self.extra_servers)
            self.start_additional_servers(self.extra_servers)
            # Give sometime for the additional server to come up.
            time.sleep(25)
            self.log.info("Pool Version at the beginning %s", pver_begin)
            output = self.dmg_command.pool_extend(self.pool.uuid,
                                                  rank, scm_size,
                                                  nvme_size)
            self.log.info(output)
            self.is_rebuild_done(3)
            self.assert_on_rebuild_failure()

            pver_extend = self.get_pool_version()
            self.log.info("Pool Version after extend %s", pver_extend)
            # Check pool version incremented after pool exclude
            self.assertTrue(pver_extend > pver_begin,
                            "Pool Version Error:  After extend")
            # Wait to finish the threads
            for thrd in threads:
                thrd.join(timeout=20)

        # Check data consistency for IOR in future
        # Presently, we are running daos_racer in parallel
        # to IOR and checking the data consistency only
        # for the daos_racer objects after exclude
        # and reintegration.
        if racer is True:
            daos_racer_thread.join()

        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            self.pool = pool[val]
            self.pool.display_pool_daos_space(display_string)
            pool[val].destroy()

    @skipForTicket("DAOS-5869")
    def test_osa_online_extend(self):
        """Test ID: DAOS-4751
        Test Description: Validate Online extend

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2
        :avocado: tags=osa,osa_extend,online_extend
        """
        # Perform extend testing with 1 to 2 pools
        self.run_online_extend_test(1)
