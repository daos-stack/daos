"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import copy
import queue
import threading
import time

from osa_utils import OSAUtils


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
                self.log.info("Stop/Start rank %s using system stop/start", kwargs["ranks"])
                ranks = str(kwargs["ranks"])
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
        # Create pools
        pools = {}
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
            self.log_step("Step 1 : Create pool")
            pools[val] = self.get_pool(connect=False)
            self.pool = pools[val]
            # Use only pool UUID while running the test.
            self.pool.use_label = False
            self.pool.set_property("reclaim", "disabled")

            self.log_step("Step 2 : Create container and write some data if data is True")
            if data:
                self.run_ior_thread("Write", oclass, test_seq)
                # Read the data back to verify it was written correctly.
                self.run_ior_thread("Read", oclass, test_seq)
                # if self.test_during_aggregation is set,
                # Create another container and run the IOR
                # command using the second container.
                if self.test_during_aggregation is True:
                    self.run_ior_thread("Write", oclass, test_seq)

        # Start the additional servers and extend the pool
        self.log_step("Step 3 : Start additional servers")
        self.log.info("Extra Servers = %s", self.extra_servers)
        self.start_additional_servers(self.extra_servers)
        extra_ranks = list(self.server_managers[-1].ranks.keys())

        self.log_step("Step 4 : Perform OSA operations in parallel")
        # Exclude and reintegrate the pool_uuid, rank and targets
        for val in range(0, num_pool):
            threads = []
            self.pool = pools[val]
            initial_total_targets = self.pool.get_total_targets(refresh=True)
            pver_begin = self.pool.get_version(True)
            self.log.info("Pool Version at the beginning %s", pver_begin)
            # If we need to trigger aggregation on pool 1, delete
            # the second container which has IOR data.
            if self.test_during_aggregation is True and val == 0:
                self.delete_extra_container(self.pool)
            # Action dictionary with OSA dmg command parameters
            action_kwargs = {
                "drain": {"pool": self.pool.identifier, "ranks": rank, "tgt_idx": None},
                "exclude": {"pool": self.pool.identifier, "ranks": (rank + 1), "tgt_idx": t_string},
                "reintegrate": {
                    "pool": self.pool.identifier, "ranks": (rank + 1), "tgt_idx": t_string},
                "extend": {"pool": self.pool.identifier, "ranks": extra_ranks}
            }
            for action in sorted(action_kwargs):
                # Add a dmg thread
                kwargs = action_kwargs[action].copy()
                kwargs['action'] = action
                kwargs['results'] = self.out_queue
                process = threading.Thread(target=self.dmg_thread, kwargs=kwargs)
                # Wait for a short period before starting the next thread
                time.sleep(5)
                self.log.info("Pool %s initiated", action)
                process.start()
                threads.append(process)

            # Wait to finish the threads
            for thread in threads:
                thread.join()

            # Verify the queue result and make sure test has no failure
            while not self.out_queue.empty():
                failure = self.out_queue.get()
                if "failed" in failure:
                    self.fail("Test failed : {0}".format(failure))

        self.log_step("Step 5 : Verify pool version and total targets after OSA operations")
        for val in range(0, num_pool):
            self.pool = pools[val]
            self.pool.wait_for_rebuild_to_end(3)
            self.assert_on_rebuild_failure()
            pver_end = self.pool.get_version()
            self.log.info("Pool Version at the End %s", pver_end)
            self.assertGreaterEqual(pver_end, 44,
                                    "Pool Version Error: {} at the end < 44".format(pver_end))

            # Extend adds targets, so the total should have grown since the beginning
            final_total_targets = self.pool.get_total_targets(refresh=True)
            self.assertGreater(final_total_targets, initial_total_targets,
                               "Pool total_targets did not increase after extend")

        self.log_step("Step 6 : Verify data integrity after OSA operations")
        # Finally run IOR to read the data and perform daos_container_check
        for val in range(0, num_pool):
            self.pool = pools[val]
            if data:
                # Presently, we support only two containers per pool.
                for c_val in range(2):
                    if self.pool_cont_dict[self.pool][c_val + 1] == "Updated":
                        self.container = self.pool_cont_dict[self.pool][c_val]
                        self.run_ior_thread("Read", oclass, test_seq, single_cont_read=False)
                        self.log.info("Checking data integrity for container %s",
                                      self.container)
                        self.container.check()

    def test_osa_offline_parallel_test(self):
        """JIRA ID: DAOS-4752.

        Test Description: Runs multiple OSA commands in parallel.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=osa,checksum,offline_parallel
        :avocado: tags=OSAOfflineParallelTest,test_osa_offline_parallel_test
        """
        self.log.info("Offline Parallel Test: Basic Test")
        self.run_offline_parallel_test(1, data=True)

    def test_osa_offline_parallel_test_without_csum(self):
        """JIRA ID: DAOS-7161.

        Test Description: Runs multiple OSA commands in parallel without enabling checksum.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
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
        :avocado: tags=hw,large
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
        :avocado: tags=hw,large
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
        :avocado: tags=hw,large
        :avocado: tags=osa,offline_parallel
        :avocado: tags=OSAOfflineParallelTest,test_osa_offline_parallel_test_oclass
        """
        self.log.info("Offline Parallel Test : OClass")
        # Presently, the script is limited and supports only one extra
        # object class testing. We are testing S1 apart from RP_2G1.
        self.run_offline_parallel_test(1, data=True,
                                       oclass=self.test_oclass[0])
