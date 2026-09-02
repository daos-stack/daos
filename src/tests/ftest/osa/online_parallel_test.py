"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import copy
import queue
import threading
import time

from daos_racer_utils import DaosRacerCommand
from exception_utils import CommandFailure
from osa_utils import OSAUtils
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
        self.ior_write_flags = self.params.get("write_flags", '/run/ior/iorflags/*')
        self.ior_read_flags = self.params.get("read_flags", '/run/ior/iorflags/*')
        self.ior_test_sequence = self.params.get("ior_test_sequence", '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get("obj_class", '/run/ior/iorflags/*')
        # Start an additional server.
        self.extra_servers = self.get_hosts_from_yaml(
            "test_servers", "server_partition", "server_reservation", "/run/extra_servers/*")
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
                time.sleep(30)
            # For each action, read the values from the
            # dictionary.
            # example {"exclude" : {"puuid": self.pool, "ranks: rank
            #                       "target": t_string, "action": exclude}}
            # getattr is used to obtain the method in dmg object.
            # eg: dmg -> pool_exclude method, then pass arguments like
            # puuid, rank, target to the pool_exclude method.
            # Add some delay between each dmg command.
            getattr(dmg, "pool_{}".format(action))(**action_args[action])
        except CommandFailure:
            results.put("{} failed".format(action_args[action]))

    def run_online_parallel_test(self, num_pool, racer=False):
        """Run multiple OSA commands / IO in parallel.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            racer (bool) : whether to start the daos_racer thread. Defaults to False.
        """
        # Create pools
        pools = {}
        target_list = []

        # Exclude target : random two targets  (target idx : 0-7)
        exc = self.random.randint(0, 6)
        target_list.append(exc)
        target_list.append(exc + 1)
        t_string = "{},{}".format(target_list[0], target_list[1])
        oclass = self.ior_dfs_oclass[0]
        test_seq = self.ior_test_sequence[0]
        # Exclude rank 2.
        rank = 2

        # Start the daos_racer thread
        if racer is True:
            kwargs = {"results": self.ds_racer_queue}
            daos_racer_thread = threading.Thread(target=self.daos_racer_thread, kwargs=kwargs)
            daos_racer_thread.start()
            time.sleep(30)

        for val in range(0, num_pool):
            self.log_step("Step 1 : Create pool")
            pools[val] = self.get_pool(connect=False)
            self.pool = pools[val]
            # Use only pool UUID while running the test.
            self.pool.use_label = False
            self.pool.set_property("reclaim", "disabled")

        # Start the additional servers and extend the pool
        self.log_step("Step 2 : Start additional servers")
        self.log.info("Extra Servers = %s", self.extra_servers)
        self.start_additional_servers(self.extra_servers)
        extra_ranks = list(self.server_managers[-1].ranks.keys())

        # Exclude and reintegrate the pool_uuid, rank and targets
        for val in range(0, num_pool):
            self.pool = pools[val]
            initial_total_targets = self.pool.get_total_targets(refresh=True)
            pver_begin = self.pool.get_version(True)
            self.log.info("Pool Version at the beginning %s", pver_begin)
            dmg_threads = []
            test_seq = self.ior_test_sequence[0]
            # Action dictionary with OSA dmg command parameters
            action_args = {
                "drain": {"pool": self.pool.identifier, "ranks": rank,
                          "tgt_idx": None},
                "exclude": {"pool": self.pool.identifier,
                            "ranks": (rank + 1),
                            "tgt_idx": t_string},
                "reintegrate": {"pool": self.pool.identifier,
                                "ranks": (rank + 1),
                                "tgt_idx": t_string},
                "extend": {"pool": self.pool.identifier,
                           "ranks": ",".join(map(str, extra_ranks))}
            }
            # Create some data before starting the OSA commands in parallel with IOR.
            if self.test_during_aggregation:
                self.log_step(
                    "Step 2a : Write some data and delete container to "
                    "initiate aggregation")
                for _ in range(0, 2):
                    self.run_ior_thread("Write", oclass, test_seq)
                self.delete_extra_container(self.pool)

            self.log_step("Step 3 : Run OSA commands in parallel with IOR")

            # Add a thread for IOR
            ior_thread = threading.Thread(target=self.run_ior_thread,
                                          kwargs={"action": "Write",
                                                  "oclass": oclass,
                                                  "test": test_seq})

            for action in sorted(action_args):
                # Add dmg threads
                dmg_threads.append(threading.Thread(target=self.dmg_thread,
                                                    kwargs={"action": action,
                                                            "action_args": action_args,
                                                            "results": self.out_queue}))
            # Launch the ior thread
            ior_thread.start()

            # Wait for the IOR to start before issuing the dmg commands
            time.sleep(10)

            # Launch dmg threads
            for dmg_thrd in dmg_threads:
                dmg_thrd.start()

            # Wait to finish the dmg threads
            for dmg_thrd in dmg_threads:
                dmg_thrd.join()

            # Wait to finish the ior thread
            ior_thread.join()

            # Check data consistency for IOR in future
            # Presently, we are running daos_racer in parallel
            # to IOR and checking the data consistency only
            # for the daos_racer objects after exclude
            # and reintegration.
            if racer is True:
                daos_racer_thread.join()

        self.log_step("Step 4 : Check pool version and total targets after extend")
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

        self.log_step("Step 5 : Check data consistency")
        # Perform a data consistency check.
        for val in range(0, num_pool):
            self.pool = pools[val]
            # Presently, we support only two containers per pool.
            for c_val in range(2):
                if self.pool_cont_dict[self.pool][c_val + 1] == "Updated":
                    self.container = self.pool_cont_dict[self.pool][c_val]
                    self.run_ior_thread("Read", oclass, test_seq, single_cont_read=False)
                    self.log.info("Checking data integrity for container %s", self.container)
                    self.container.check()

    def test_osa_online_parallel_test(self):
        """
        JIRA ID: DAOS-4752

        Test Description: Runs multiple OSA commands/IO in parallel

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=osa,checksum,osa_parallel
        :avocado: tags=OSAOnlineParallelTest,test_osa_online_parallel_test
        """
        self.run_online_parallel_test(1)

    def test_osa_online_parallel_test_with_aggregation(self):
        """
        JIRA ID: DAOS-4752

        Test Description: Runs multiple OSA commands/IO with aggregation turned on.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=osa,checksum,osa_parallel
        :avocado: tags=OSAOnlineParallelTest,test_osa_online_parallel_test_with_aggregation
        """
        self.test_during_aggregation = self.params.get("test_with_aggregation",
                                                       '/run/aggregation/*')
        self.log.info("Online Parallel Test : Aggregation")
        self.run_online_parallel_test(1)
