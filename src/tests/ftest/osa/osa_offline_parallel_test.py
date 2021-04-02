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
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from command_utils import CommandFailure
from apricot import skipForTicket
import queue


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
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.daos_command = DaosCommand(self.bin)
        self.ior_test_sequence = self.params.get("ior_test_sequence",
                                                 '/run/ior/iorflags/*')
        # Start an additional server.
        self.extra_servers = self.params.get("test_servers",
                                             "/run/extra_servers/*")
        self.out_queue = queue.Queue()
        self.dmg_command.exit_status_exception = True

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

    def run_offline_parallel_test(self, num_pool, data=False, oclass=None):
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
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value

        # Exclude target : random two targets (target idx : 0-7)
        n = random.randint(0, 6)
        target_list.append(n)
        target_list.append(n+1)
        t_string = "{},{}".format(target_list[0], target_list[1])

        # Exclude rank 2.
        rank = 2

        test_seq = self.ior_test_sequence[0]
        for val in range(0, num_pool):
            pool[val] = TestPool(self.context,
                                 dmg_command=self.get_dmg_command())
            pool[val].get_params(self)
            if val == 0:
                # Start the additional servers and extend the pool
                self.log.info("Extra Servers = %s", self.extra_servers)
                self.start_additional_servers(self.extra_servers)
            # Split total SCM and NVME size for creating multiple pools.
            pool[val].scm_size.value = int(pool[val].scm_size.value /
                                           num_pool)
            pool[val].nvme_size.value = int(pool[val].nvme_size.value /
                                            num_pool)
            pool[val].create()
            pool_uuid.append(pool[val].uuid)
            self.pool = pool[val]
            if data:
                self.run_ior_thread("Write", oclass, test_seq)
                self.run_mdtest_thread()

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
                                "tgt_idx": t_string},
                "extend": {"pool": self.pool.uuid, "rank": (rank + 2)}
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
            self.run_ior_thread("Read", oclass, test_seq)
            self.run_mdtest_thread()
            self.container = self.pool_cont_dict[self.pool][0]
            kwargs = {"pool": self.pool.uuid,
                      "cont": self.container.uuid}
            output = self.daos_command.container_check(**kwargs)
            self.log.info(output)

    def test_osa_offline_parallel_test(self):
        """
        JIRA ID: DAOS-4752

        Test Description: Runs multiple OSA commands in parallel.

        :avocado: tags=all,daily_regression,hw,medium,ib2
        :avocado: tags=osa,osa_parallel
        :avocado: tags=osa_parallel_basic_test
        """
        # Run the parallel offline test.
        self.log.info("Offline Parallel Test: Basic Test")
        self.run_offline_parallel_test(1, True)

    def test_osa_offline_parallel_test_no_csum(self):
        """
        JIRA ID: DAOS-7161

        Test Description: Runs multiple OSA commands in parallel.

        :avocado: tags=all,daily_regression,hw,medium,ib2
        :avocado: tags=osa,offline_parallel
        :avocado: tags=offline_parallel_without_csum
        """
        self.test_with_checksum = self.params.get("test_with_checksum",
                                                  '/run/checksum/*')
        self.log.info("Offline Parallel Test: Without Checksum")
        # Run the parallel offline test.
        self.run_offline_parallel_test(1, True)
