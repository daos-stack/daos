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
from dmg_utils import check_system_query_status
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
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        self.out_queue = queue.Queue()
        self.dmg_command.exit_status_exception = True
        self.server_boot = None

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
                text = "Waiting for rebuild to complete"
                time.sleep(3)
                self.print_and_assert_on_rebuild_failure(text)
                # For each action, read the values from the
                # dictionary.
                # example {"exclude" : {"puuid": self.pool, "rank": rank
                #                       "target": t_string, "action": exclude}}
                # getattr is used to obtain the method in dmg object.
                # eg: dmg -> pool_exclude method, then pass arguments like
                # puuid, rank, target to the pool_exclude method.
            if action == "exclude" and self.server_boot is True:
                ranks = action_args[action][1]
                getattr(dmg, "system stop --ranks={}".format(ranks))
                output = "Stopping the rank : {}".format(ranks)
                self.print_and_assert_on_rebuild_failure(output)
                getattr(dmg, "system start --ranks={}".format(ranks))
                self.print_and_assert_on_rebuild_failure(output)
            else:
                getattr(dmg, "pool_{}".format(action))(**action_args[action])
        except CommandFailure as _error:
            results.put("{} failed".format(action))

    def run_offline_parallel_test(self, num_pool, data=False, oclass=None):
        """Run multiple OSA commands in parallel with or without data.
            Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
            oclass (str) : Daos object class (RP_2G1,etc)
        """
        # Create a pool
        self.pool = []
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
            self.pool.append(self.get_pool())
            pool_uuid.append(self.pool[-1].uuid)
            self.pool[-1].set_property("reclaim", "disabled")
            if data:
                self.run_ior_thread("Write", oclass, test_seq)
                if oclass != "S1":
                    self.run_mdtest_thread()
                # if self.test_during_aggregation is set,
                # Create another container and run the IOR
                # command using the second container.
                if self.test_during_aggregation is True:
                    self.run_ior_thread("Write", oclass, test_seq)

        # Start the additional servers and extend the pool
        self.log.info("Extra Servers = %s", self.extra_servers)
        self.start_additional_servers(self.extra_servers)
        # Give sometime for the additional server to come up.
        for retry in range(0, 10):
            scan_info = self.get_dmg_command().system_query()
            if not check_system_query_status(scan_info):
                if retry == 9:
                    self.fail("One or more servers not in expected status")
            else:
                break

        # Exclude and reintegrate the pool_uuid, rank and targets
        for val in range(0, num_pool):
            self.pool[val].display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            # If we need to trigger aggregation on pool 1, delete
            # the second container which has IOR data.
            if self.test_during_aggregation is True and val == 0:
                self.delete_extra_container(self.pool[val])
            # Create the threads here
            threads = []
            # Action dictionary with OSA dmg command parameters
            action_args = {
                "drain": {"pool": self.pool[val].uuid, "rank": rank,
                          "tgt_idx": None},
                "exclude": {"pool": self.pool[val].uuid, "rank": (rank + 1),
                            "tgt_idx": t_string},
                "reintegrate": {"pool": self.pool[val].uuid, "rank": (rank + 1),
                                "tgt_idx": t_string},
                "extend": {"pool": self.pool[val].uuid, "ranks": (rank + 2),
                           "scm_size": self.pool[val].scm_size,
                           "nvme_size": self.pool[val].nvme_size}
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
            self.pool[val].display_pool_daos_space(display_string)
            self.is_rebuild_done(3)
            self.assert_on_rebuild_failure()
            pver_end = self.get_pool_version()
            self.log.info("Pool Version at the End %s", pver_end)
            self.assertTrue(pver_end >= 26,
                            "Pool Version Error:  at the end")
        if data:
            self.run_ior_thread("Read", oclass, test_seq)
            if oclass != "S1":
                self.run_mdtest_thread()
            self.container = self.pool_cont_dict[self.pool[-1]][0]
            kwargs = {"pool": self.pool[-1].uuid,
                      "cont": self.container.uuid}
            output = self.daos_command.container_check(**kwargs)
            self.log.info(output)

    @skipForTicket("DAOS-7247")
    def test_osa_offline_parallel_test(self):
        """
        JIRA ID: DAOS-4752

        Test Description: Runs multiple OSA commands in parallel.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa,checksum
        :avocado: tags=offline_parallel,offline_parallel_basic_test
        """
        self.log.info("Offline Parallel Test: Basic Test")
        self.run_offline_parallel_test(1, data=True)

    @skipForTicket("DAOS-7247")
    def test_osa_offline_parallel_test_without_csum(self):
        """
        JIRA ID: DAOS-7161

        Test Description: Runs multiple OSA commands in parallel
        without enabling checksum.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa
        :avocado: tags=offline_parallel,offline_parallel_without_csum
        """
        self.test_with_checksum = self.params.get("test_with_checksum",
                                                  '/run/checksum/*')
        self.log.info("Offline Parallel Test: Without Checksum")
        self.run_offline_parallel_test(1, data=True)

    @skipForTicket("DAOS-7247")
    def test_osa_offline_parallel_test_rank_boot(self):
        """
        JIRA ID: DAOS-7161

        Test Description: Runs multiple OSA commands in parallel
        with a rank rebooted using system stop/start.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa
        :avocado: tags=offline_parallel,offline_parallel_srv_rank_boot
        """
        self.test_with_checksum = self.params.get("test_with_checksum",
                                                  '/run/checksum/*')
        self.server_boot = self.params.get("flags",
                                           '/run/system_stop_start/*')
        self.log.info("Offline Parallel Test: Restart a rank")
        self.run_offline_parallel_test(1, data=True)

    @skipForTicket("DAOS-7195,DAOS-7247")
    def test_osa_offline_parallel_test_with_aggregation(self):
        """
        JIRA ID: DAOS-7161

        Test Description: Runs multiple OSA commands in parallel
        with aggregation turned on.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa
        :avocado: tags=offline_parallel,offline_parallel_with_aggregation
        """
        self.test_during_aggregation = self.params.get("test_with_aggregation",
                                                       '/run/aggregation/*')
        self.log.info("Offline Parallel Test : Aggregation")
        self.run_offline_parallel_test(1, data=True)

    @skipForTicket("DAOS-7247")
    def test_osa_offline_parallel_test_oclass(self):
        """
        JIRA ID: DAOS-7161

        Test Description: Runs multiple OSA commands in parallel
        with different object class.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=osa
        :avocado: tags=offline_parallel,offline_parallel_oclass
        """
        self.log.info("Offline Parallel Test : OClass")
        # Presently, the script is limited and supports only one extra
        # object class testing. We are testing S1 apart from RP_2G1.
        self.run_offline_parallel_test(1, data=True,
                                       oclass=self.test_oclass[0])
