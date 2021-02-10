#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import random
import threading
import copy
from osa_utils import OSAUtils
from test_utils_pool import TestPool
from command_utils import CommandFailure
from apricot import skipForTicket

try:
    # python 3.x
    import queue as queue
except ImportError:
    # python 2.7
    import Queue as queue

class OSAOfflineParallelTest(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline drain,reintegration,
    extend test cases in parallel.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(OSAOfflineParallelTest, self).setUp()
        self.dmg_command = self.get_dmg_command()
        self.out_queue = queue.Queue()

    def dmg_thread(self, action, action_args, results):
        """Generate different dmg command related to OSA.
            Args:
            action_args(dict) : {action: {"puuid":
                                          pool[val].uuid,
                                          "rank": rank,
                                          "target": t_string,
                                          "action": action,}
            results (queue) : dmg command output queue.
        """
        dmg = copy.copy(self.dmg_command)
        try:
            if action == "reintegrate":
                time.sleep(60)
                # For each action, read the values from the
                # dictionary.
                # example {"exclude" : {"puuid": self.pool, "rank": rank
                #                       "target": t_string, "action": exclude}}
                # getattr is used to obtain the method in dmg object.
                # eg: dmg -> pool_exclude method, then pass arguments like
                # puuid, rank, target to the pool_exclude method.
            getattr(dmg, "pool_{}".format(action))(**action_args[action])
        except CommandFailure as _error:
            results.put("{} failed".format(action))
            # Future enhancement for extend
            # elif action == "extend":
            #    dmg.pool_extend(puuid, (rank + 2))

    def run_offline_parallel_test(self, num_pool, data=False):
        """Run multiple OSA commands in parallel with or without data.
            Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
        """
        # Create a pool
        pool = {}
        pool_uuid = []
        target_list = []

        # Exclude target : random two targets (target idx : 0-7)
        n = random.randint(0, 6)
        target_list.append(n)
        target_list.append(n+1)
        t_string = "{},{}".format(target_list[0], target_list[1])

        # Exclude rank 2.
        rank = 2

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
                self.write_single_object()

        # Exclude and reintegrate the pool_uuid, rank and targets
        for val in range(0, num_pool):
            self.pool = pool[val]
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            # Create the threads here
            threads = []
            # Action dictionary with OSA dmg command parameters
            action_args = {
                "drain": {"pool": self.pool.uuid, "rank": rank,
                          "tgt_idx": None},
                "exclude": {"pool": self.pool.uuid, "rank": (rank + 1),
                            "tgt_idx": t_string},
                "reintegrate": {"pool": self.pool.uuid, "rank": (rank + 1),
                                "tgt_idx": t_string}
            }
            for action in sorted(action_args):
                # Add a dmg thread
                process = threading.Thread(target=self.dmg_thread,
                                           kwargs={"action": action,
                                                   "action_args":
                                                   action_args,
                                                   "results":
                                                   self.out_queue})
                process.start()
                threads.append(process)

        # Wait to finish the threads
        for thrd in threads:
            thrd.join()
            time.sleep(5)

        # Check the queue for any failure.
        tmp_list = list(self.out_queue.queue)
        for failure in tmp_list:
            if "FAIL" in failure:
                self.fail("Test failed : {0}".format(failure))

        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            pool[val].display_pool_daos_space(display_string)
            self.is_rebuild_done(3)
            self.assert_on_rebuild_failure()
            pver_end = self.get_pool_version()
            self.log.info("Pool Version at the End %s", pver_end)
            self.assertTrue(pver_end == 25,
                            "Pool Version Error:  at the end")
        if data:
            self.verify_single_object()

    @skipForTicket("DAOS-6668")
    def test_osa_offline_parallel_test(self):
        """
        JIRA ID: DAOS-4752

        Test Description: Runs multiple OSA commands in parallel.

        :avocado: tags=all,daily_regression,hw,medium,ib2
        :avocado: tags=osa,osa_parallel,offline_parallel
        """
        # Run the parallel offline test.
        self.run_offline_parallel_test(1, True)
