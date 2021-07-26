#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import random
import threading

from itertools import product
from write_host_file import write_host_file
from daos_racer_utils import DaosRacerCommand
from osa_utils import OSAUtils
from apricot import skipForTicket
import queue


class OSAOnlineReintegration(OSAUtils):
    # pylint: disable=too-many-ancestors
    """Online Server Addition online re-integration test class.

    Test Class Description:
        This test runs the daos_server Online reintegration test cases.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_test_sequence = self.params.get(
            "ior_test_sequence", '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get(
            "obj_class", '/run/ior/iorflags/*')
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

    def run_online_reintegration_test(self, num_pool, racer=False,
                                      server_boot=False):
        """Run the Online reintegration without data.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
            server_boot (bool) : Perform system stop/start on a rank.
                                 Defults to False.
        """
        num_jobs = self.params.get("no_parallel_job", '/run/ior/*')
        # Create a pool
        self.pool = []
        pool_uuid = []
        exclude_servers = (len(self.hostlist_servers) * 2) - 1

        # Exclude one rank : other than rank 0.
        rank = random.randint(1, exclude_servers)

        # Start the daos_racer thread
        if racer is True:
            daos_racer_thread = threading.Thread(target=self.daos_racer_thread)
            daos_racer_thread.start()
            time.sleep(30)

        for val in range(0, num_pool):
            self.pool.append(self.get_pool(create=False))
            # Split total SCM and NVME size for creating multiple pools.
            self.pool[-1].scm_size.value = int(
                self.pool[-1].scm_size.value / num_pool)
            self.pool[-1].nvme_size.value = int(
                self.pool[-1].nvme_size.value / num_pool)
            self.pool[-1].create()
            pool_uuid.append(self.pool[-1].uuid)

        # Exclude and reintegrate the pool_uuid, rank and targets
        for val in range(0, num_pool):
            threads = []
            for oclass, api, test, flags in product(self.ior_dfs_oclass,
                                                    self.ior_apis,
                                                    self.ior_test_sequence,
                                                    self.ior_flags):
                for _ in range(0, num_jobs):
                    # Add a thread for these IOR arguments
                    threads.append(threading.Thread(target=self.ior_thread,
                                                    kwargs={
                                                        "pool": self.pool[val],
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
            time.sleep(5)
            self.pool[val].display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            if server_boot is False:
                output = self.dmg_command.pool_exclude(
                    self.pool[val].uuid, rank)
            else:
                output = self.dmg_command.system_stop(ranks=rank)
                self.pool[val].wait_for_rebuild(True)
                self.log.info(output)
                output = self.dmg_command.system_start(ranks=rank)

            self.log.info(output)
            self.is_rebuild_done(3)
            self.assert_on_rebuild_failure()
            pver_exclude = self.get_pool_version()
            time.sleep(5)

            self.log.info("Pool Version after exclude %s", pver_exclude)
            # Check pool version incremented after pool exclude
            # pver_exclude should be greater than
            # pver_begin + 8 targets.
            self.assertTrue(pver_exclude > (pver_begin + 8),
                            "Pool Version Error:  After exclude")
            output = self.dmg_command.pool_reintegrate(
                self.pool[val].uuid, rank)
            self.log.info(output)
            self.is_rebuild_done(3)
            self.assert_on_rebuild_failure()

            pver_reint = self.get_pool_version()
            self.log.info("Pool Version after reintegrate %d", pver_reint)
            # Check pool version incremented after pool reintegrate
            self.assertTrue(pver_reint > (pver_exclude + 1),
                            "Pool Version Error:  After reintegrate")
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
            self.pool[val].display_pool_daos_space(display_string)

    @skipForTicket("DAOS-6573")
    def test_osa_online_reintegration(self):
        """Test ID: DAOS-5075.

        Test Description: Validate Online Reintegration

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa,checksum
        :avocado: tags=online_reintegration
        """
        # Perform reintegration testing with 1 pool.
        for pool_num in range(1, 2):
            self.run_online_reintegration_test(pool_num)

    @skipForTicket("DAOS-6766, DAOS-6783")
    def test_osa_online_reintegration_server_stop(self):
        """Test ID: DAOS-5920.
        Test Description: Validate Online Reintegration with server stop
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa,checksum
        :avocado: tags=online_reintegration_srv_stop
        """
        self.run_online_reintegration_test(1, server_boot=True)
