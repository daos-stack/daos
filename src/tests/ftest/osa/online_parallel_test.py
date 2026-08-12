"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import copy
import queue
import threading
import time
from itertools import product

from daos_racer_utils import DaosRacerCommand
from exception_utils import CommandFailure
from osa_utils import OSAUtils
from test_utils_pool import add_pool
from write_host_file import write_host_file


class OSAOnlineParallelTest(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server online drain,reintegration,
    extend test cases in parallel.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_flags = self.params.get("ior_flags", '/run/ior/iorflags/*')
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_test_sequence = self.params.get("ior_test_sequence", '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get("obj_class", '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(self.hostlist_clients, self.workdir)
        self.pool = None
        self.out_queue = queue.Queue()
        self.ds_racer_queue = queue.Queue()
        self.daos_racer = None

    def daos_racer_thread(self, results):
        """Start the daos_racer thread."""
        self.daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients[0], self.dmg_command)
        self.daos_racer.get_params(self)
        self.daos_racer.run()
        results.put("Daos Racer Started")

    def dmg_thread(self, action, action_args, results):
        """Generate different dmg command related to OSA.

        Args:
            action_args(dict) : {action: {"puuid":
                                          pool[val].uuid,
                                          "ranks": rank,
                                          "target": t_string,
                                          "action": action,}
            results (queue) : dmg command output queue.
        """
        # Give sometime for IOR threads to start
        dmg = copy.copy(self.dmg_command)
        try:
            if action == "reintegrate":
                time.sleep(60)
            # For each action, read the values from the
            # dictionary.
            # example {"exclude" : {"puuid": self.pool, "ranks: rank
            #                       "target": t_string, "action": exclude}}
            # getattr is used to obtain the method in dmg object.
            # eg: dmg -> pool_exclude method, then pass arguments like
            # puuid, rank, target to the pool_exclude method.
            getattr(dmg, "pool_{}".format(action))(**action_args[action])
        except CommandFailure:
            results.put("{} failed".format(action_args[action]))
        # Future enhancement for extend
        # elif action == "extend":
        #    dmg.pool_extend(puuid, (rank + 2))

    def run_online_parallel_test(self, num_pool, racer=False):
        """Run multiple OSA commands / IO in parallel.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
        """
        num_jobs = self.params.get("no_parallel_job", '/run/ior/*')
        # Create a pool
        pool = {}
        target_list = []

        # Exclude target : random two targets  (target idx : 0-7)
        exc = self.random.randint(0, 6)
        target_list.append(exc)
        target_list.append(exc + 1)
        t_string = "{},{}".format(target_list[0], target_list[1])

        # Exclude rank 2.
        rank = 2

        all_ranks = list(self.server_managers[0].ranks.keys())
        total_ranks = len(all_ranks)

        # Start the daos_racer thread
        if racer is True:
            kwargs = {"results": self.ds_racer_queue}
            daos_racer_thread = threading.Thread(target=self.daos_racer_thread, kwargs=kwargs)
            daos_racer_thread.start()
            time.sleep(30)

        for val in range(0, num_pool):
            pool[val] = add_pool(self, connect=False)
            self.pool = pool[val]
            # Use only pool UUID while running the test.
            self.pool.use_label = False
            self.pool.set_property("reclaim", "disabled")

        # Exclude and reintegrate the pool_uuid, rank and targets
        for val in range(0, num_pool):
            self.pool = pool[val]
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.pool.get_version(True)
            self.log.info("Pool Version at the beginning %s", pver_begin)
            threads = []
            for oclass, api, test, flags in product(self.ior_dfs_oclass,
                                                    self.ior_apis,
                                                    self.ior_test_sequence,
                                                    self.ior_flags):
                # Action dictionary with OSA dmg command parameters
                action_kwargs = {
                    "drain": {"pool": self.pool.identifier, "ranks": rank,
                              "tgt_idx": None},
                    "exclude": {"pool": self.pool.identifier,
                                "ranks": (rank + 1),
                                "tgt_idx": t_string},
                    "reintegrate": {"pool": self.pool.identifier,
                                    "ranks": (rank + 1),
                                    "tgt_idx": t_string},
                    "extend": {"pool": self.pool.identifier,
                               "ranks": (total_ranks + 1)}
                }
                for _ in range(0, num_jobs):
                    # Add a thread for these IOR arguments
                    threads.append(threading.Thread(target=self.ior_thread,
                                                    kwargs={
                                                        "pool": self.pool.identifier,
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
                    time.sleep(2)

                for action in sorted(action_kwargs):
                    # Add a dmg thread
                    kwargs = action_kwargs[action].copy()
                    kwargs['action'] = action
                    kwargs['results'] = self.out_queue
                    process = threading.Thread(target=self.dmg_thread, kwargs=kwargs)
                    self.log.info("Starting pool %s in a thread", action)
                    process.start()
                    threads.append(process)

                # Wait to finish the threads (dmg commands to get executed)
                for thrd in threads:
                    thrd.join(timeout=100)

            # Check data consistency for IOR in future
            # Presently, we are running daos_racer in parallel
            # to IOR and checking the data consistency only
            # for the daos_racer objects after exclude
            # and reintegration.
            if racer is True:
                daos_racer_thread.join()

            for val in range(0, num_pool):
                self.pool = pool[val]
                display_string = "Pool{} space at the End".format(val)
                self.pool.display_pool_daos_space(display_string)
                self.pool.wait_for_rebuild_to_end(3)
                self.assert_on_rebuild_failure()

                pver_end = self.pool.get_version()
                self.log.info("Pool Version at the End %s", pver_end)
                self.assertTrue(pver_end == 25, "Pool Version Error:  at the end")

    def test_osa_online_parallel_test(self):
        """
        JIRA ID: DAOS-4752

        Test Description: Runs multiple OSA commands/IO in parallel

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=osa,checksum,osa_parallel
        :avocado: tags=OSAOnlineParallelTest,test_osa_online_parallel_test
        """
        self.run_online_parallel_test(1)
