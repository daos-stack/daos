"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import copy
import queue
import threading
import time

from dmg_utils import check_system_query_status
from osa_utils import OSAUtils
from test_utils_pool import add_pool


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
        self.ior_test_sequence = self.params.get("ior_test_sequence", '/run/ior/iorflags/*')
        # Start an additional server.
        self.extra_servers = self.get_hosts_from_yaml(
            "test_servers", "server_partition", "server_reservation", "/run/extra_servers/*")
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        self.out_queue = queue.Queue()
        self.dmg_command.exit_status_exception = True
        self.server_boot = None

    def dmg_thread(self, action, results, **kwargs):
        """Generate different dmg command related to OSA.

        Args:
            action (str): dmg subcommand string such as drain, exclude, extend.
            results (queue): dmg command output queue to store results.
            kwargs (dict): Parameters for the dmg command methods in dmg_utils.py, plus
                'action' and 'results' params above.
        """
        dmg = copy.copy(self.dmg_command)
        try:
            if action == "reintegrate":
                text = "Waiting for rebuild to complete before pool reintegrate"
                time.sleep(3)
                self.print_and_assert_on_rebuild_failure(text)
            if action == "exclude" and self.server_boot is True:
                ranks = str(kwargs["rank"])
                dmg.system_stop(ranks=ranks)
                self.print_and_assert_on_rebuild_failure("Stopping rank {}".format(ranks))
                dmg.system_start(ranks=ranks)
                self.print_and_assert_on_rebuild_failure("Starting rank {}".format(ranks))
            else:
                # For each action, pass in necessary parameters to the dmg method with
                # kwargs. getattr is used to obtain the method in dmg object.
                # eg: dmg -> pool_exclude method, then pass arguments like
                # puuid, rank, target to the pool_exclude method.
                getattr(dmg, "pool_{}".format(action))(**kwargs)
        except Exception as error:      # pylint: disable=broad-except
            results.put("pool {} failed: {}".format(action, str(error)))

    def run_offline_parallel_test(self, num_pool, data=False, oclass=None):
        """Run multiple OSA commands in parallel with or without data.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create some data in pool. Defaults to
                False.
            oclass (str) : Daos object class (RP_2G1,etc)
        """
        # Create a pool
        pool = {}
        target_list = []
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value

        # Exclude target : random two targets (target idx : 0-7)
        exc = self.random.randint(0, 6)
        target_list.append(exc)
        target_list.append(exc + 1)
        t_string = "{},{}".format(target_list[0], target_list[1])

        # Exclude rank 2.
        rank = 2

        test_seq = self.ior_test_sequence[0]
        for val in range(0, num_pool):
            pool[val] = add_pool(self, connect=False)
            self.pool = pool[val]
            # Use only pool UUID while running the test.
            self.pool.use_label = False
            self.pool.set_property("reclaim", "disabled")

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
            self.pool = pool[val]
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.pool.get_version(True)
            self.log.info("Pool Version at the beginning %s", pver_begin)
            # If we need to trigger aggregation on pool 1, delete
            # the second container which has IOR data.
            if self.test_during_aggregation is True and val == 0:
                self.delete_extra_container(self.pool)
            # Create the threads here
            threads = []
            # Action dictionary with OSA dmg command parameters
            action_kwargs = {
                "drain": {"pool": self.pool.identifier, "rank": rank, "tgt_idx": None},
                "exclude": {"pool": self.pool.identifier, "rank": (rank + 1), "tgt_idx": t_string},
                "reintegrate": {
                    "pool": self.pool.identifier, "rank": (rank + 1), "tgt_idx": t_string},
                "extend": {"pool": self.pool.identifier, "ranks": (rank + 2)}
            }
            for action in sorted(action_kwargs):
                # Add a dmg thread
                kwargs = action_kwargs[action].copy()
                kwargs['action'] = action
                kwargs['results'] = self.out_queue
                process = threading.Thread(target=self.dmg_thread, kwargs=kwargs)
                self.log.info("Starting pool %s in a thread", action)
                process.start()
                threads.append(process)

        # Wait to finish the threads
        for thread in threads:
            thread.join()
            time.sleep(5)

        # Verify the queue result and make sure test has no failure
        while not self.out_queue.empty():
            failure = self.out_queue.get()
            if "failed" in failure:
                self.fail("Test failed : {0}".format(failure))

        for val in range(0, num_pool):
            self.pool = pool[val]
            display_string = "Pool{} space at the End".format(val)
            self.pool.display_pool_daos_space(display_string)
            self.pool.wait_for_rebuild_to_end(3)
            self.assert_on_rebuild_failure()
            pver_end = self.pool.get_version(True)
            self.log.info("Pool Version at the End %s", pver_end)
            if self.server_boot is True:
                self.assertTrue(
                    pver_end >= 17, "Pool Version Error: {} at the end < 17".format(pver_end))
            else:
                self.assertTrue(
                    pver_end >= 25, "Pool Version Error: {} at the end < 25".format(pver_end))

        # Finally run IOR to read the data and perform daos_container_check
        for val in range(0, num_pool):
            self.pool = pool[val]
            if data:
                self.run_ior_thread("Read", oclass, test_seq)
                if oclass != "S1":
                    self.run_mdtest_thread()
                self.container = self.pool_cont_dict[self.pool][0]
                self.container.check()

    def test_osa_offline_parallel_test(self):
        """JIRA ID: DAOS-4752.

        Test Description: Runs multiple OSA commands in parallel.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,offline_parallel
        :avocado: tags=OSAOfflineParallelTest,test_osa_offline_parallel_test
        """
        self.log.info("Offline Parallel Test: Basic Test")
        self.run_offline_parallel_test(1, data=True)

    def test_osa_offline_parallel_test_without_csum(self):
        """JIRA ID: DAOS-7161.

        Test Description: Runs multiple OSA commands in parallel without enabling checksum.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,offline_parallel
        :avocado: tags=OSAOfflineParallelTest,test_osa_offline_parallel_test_without_csum
        """
        self.test_with_checksum = self.params.get("test_with_checksum",
                                                  '/run/checksum/*')
        self.log.info("Offline Parallel Test: Without Checksum")
        self.run_offline_parallel_test(1, data=True)

    def test_osa_offline_parallel_test_rank_boot(self):
        """JIRA ID: DAOS-7161.

        Test Description: Runs multiple OSA commands in parallel with a rank rebooted using system
        stop/start.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,offline_parallel
        :avocado: tags=OSAOfflineParallelTest,test_osa_offline_parallel_test_rank_boot
        """
        self.test_with_checksum = self.params.get("test_with_checksum", '/run/checksum/*')
        self.server_boot = self.params.get("flags", '/run/system_stop_start/*')
        self.log.info("Offline Parallel Test: Restart a rank")
        self.run_offline_parallel_test(1, data=True)

    def test_osa_offline_parallel_test_with_aggregation(self):
        """JIRA ID: DAOS-7161.

        Test Description: Runs multiple OSA commands in parallel with aggregation turned on.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,offline_parallel
        :avocado: tags=OSAOfflineParallelTest,test_osa_offline_parallel_test_with_aggregation
        """
        self.test_during_aggregation = self.params.get("test_with_aggregation",
                                                       '/run/aggregation/*')
        self.log.info("Offline Parallel Test : Aggregation")
        self.run_offline_parallel_test(1, data=True)

    def test_osa_offline_parallel_test_oclass(self):
        """JIRA ID: DAOS-7161.

        Test Description: Runs multiple OSA commands in parallel with different object class.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,offline_parallel
        :avocado: tags=OSAOfflineParallelTest,test_osa_offline_parallel_test_oclass
        """
        self.log.info("Offline Parallel Test : OClass")
        # Presently, the script is limited and supports only one extra
        # object class testing. We are testing S1 apart from RP_2G1.
        self.run_offline_parallel_test(1, data=True,
                                       oclass=self.test_oclass[0])
