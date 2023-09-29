"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
import threading

from test_utils_pool import add_pool
from write_host_file import write_host_file
from daos_racer_utils import DaosRacerCommand
from dmg_utils import check_system_query_status
from osa_utils import OSAUtils


class OSAOnlineExtend(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server Online Extend test cases.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_test_sequence = self.params.get("ior_test_sequence", '/run/ior/iorflags/*')
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        self.ranks = self.params.get("rank_list", '/run/test_ranks/*')
        # Start an additional server.
        self.extra_servers = self.get_hosts_from_yaml(
            "test_servers", "server_partition", "server_reservation", "/run/extra_servers/*")
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(self.hostlist_clients, self.workdir, None)
        self.pool = None
        self.dmg_command.exit_status_exception = True
        self.daos_racer = None

    def daos_racer_thread(self):
        """Start the daos_racer thread."""
        self.daos_racer = DaosRacerCommand(self.bin, self.hostlist_clients[0], self.dmg_command)
        self.daos_racer.get_params(self)
        self.daos_racer.run()

    def run_online_extend_test(self, num_pool, racer=False, oclass=None, app_name="ior",
                               exclude_or_drain=None):
        """Run the Online extend without data.

        Args:
             num_pool (int): total pools to create for testing purposes.
             racer (bool): Run the testing along with daos_racer. Defaults to False.
             oclass (str): Object Class (eg: RP_2G1, etc). Default to None.
             app_name (str): App (ior or mdtest) to run during the testing. Defaults to ior.
             exclude_or_drain (str): Pass "exclude" or "drain" string. Defaults to None.
        """
        # Pool dictionary
        pool = {}

        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value
        test_seq = self.ior_test_sequence[0]

        # Start the daos_racer thread
        if racer is True:
            daos_racer_thread = threading.Thread(target=self.daos_racer_thread)
            daos_racer_thread.start()
            time.sleep(30)

        for val in range(0, num_pool):
            pool[val] = add_pool(self, connect=False)
            pool[val].set_property("reclaim", "disabled")

        # Extend the pool, rank and targets
        for val in range(0, num_pool):
            threads = []
            self.pool = pool[val]

            # Start the additional servers and extend the pool
            self.log.info("Extra Servers = %s", self.extra_servers)
            self.start_additional_servers(self.extra_servers)
            if self.test_during_aggregation is True:
                for _ in range(0, 2):
                    self.run_ior_thread("Write", oclass, test_seq)
                self.delete_extra_container(self.pool)
            # The following thread runs while performing osa operations.
            if app_name == "ior":
                threads.append(threading.Thread(target=self.run_ior_thread,
                                                kwargs={"action": "Write",
                                                        "oclass": oclass,
                                                        "test": test_seq}))
            else:
                threads.append(threading.Thread(target=self.run_mdtest_thread))
            # Make sure system map has all ranks in joined state.
            for retry in range(0, 10):
                scan_info = self.get_dmg_command().system_query()
                if not check_system_query_status(scan_info):
                    if retry == 9:
                        self.fail("One or more servers not in expected status")
                else:
                    break

            # Launch the IOR or mdtest thread
            for thrd in threads:
                self.log.info("Thread : %s", thrd)
                thrd.start()
                time.sleep(1)

            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.pool.get_version(True)
            self.log.info("Pool Version at the beginning %s", pver_begin)
            # Get initial total free space (scm+nvme)
            initial_free_space = self.pool.get_total_free_space(refresh=True)
            output = self.pool.extend(self.ranks)
            self.log.info(output)
            if exclude_or_drain == "exclude":
                self.pool.wait_for_rebuild_to_start()
                # Give a 4 minute delay so that some objects are moved
                # as part of rebuild operation.
                time.sleep(4)
                self.log.info("Exclude rank 3 while rebuild is happening")
                output = self.pool.exclude("3")
            elif exclude_or_drain == "drain":
                # Drain cannot be performed while extend rebuild is happening.
                self.print_and_assert_on_rebuild_failure(output)
                self.log.info("Drain rank 3 after extend rebuild is completed")
                output = self.pool.drain("3")
            self.print_and_assert_on_rebuild_failure(output)
            free_space_after_extend = self.pool.get_total_free_space(refresh=True)

            pver_extend = self.pool.get_version(True)
            self.log.info("Pool Version after extend %s", pver_extend)
            # Check pool version incremented after pool exclude
            self.assertTrue(pver_extend > pver_begin, "Pool Version Error:  After extend")
            self.assertTrue(free_space_after_extend > initial_free_space,
                            "Expected free space after extend is less than initial")
            # Wait to finish the threads
            for thrd in threads:
                thrd.join()
                if not self.out_queue.empty():
                    self.assert_on_exception()

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
            if app_name == "ior":
                self.run_ior_thread("Read", oclass, test_seq)
            else:
                break
            self.container = self.pool_cont_dict[self.pool][0]
            self.container.check()

    def test_osa_online_extend(self):
        """Test ID: DAOS-4751.

        Test Description: Validate Online extend with checksum enabled.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,osa_extend,online_extend
        :avocado: tags=OSAOnlineExtend,test_osa_online_extend
        """
        self.log.info("Online Extend : With Checksum")
        self.run_online_extend_test(1)

    def test_osa_online_extend_without_checksum(self):
        """Test ID: DAOS-6645.

        Test Description: Validate Online extend without checksum enabled.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,osa_extend,online_extend
        :avocado: tags=OSAOnlineExtend,test_osa_online_extend_without_checksum
        """
        self.log.info("Online Extend : Without Checksum")
        self.test_with_checksum = self.params.get("test_with_checksum",
                                                  '/run/checksum/*')
        self.run_online_extend_test(1)

    def test_osa_online_extend_oclass(self):
        """Test ID: DAOS-6645.

        Test Description: Validate Online extend with different object class.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,osa_extend,online_extend
        :avocado: tags=OSAOnlineExtend,test_osa_online_extend_oclass
        """
        self.log.info("Online Extend : Oclass")
        self.run_online_extend_test(1, oclass=self.test_oclass[0])

    def test_osa_online_extend_mdtest(self):
        """Test ID: DAOS-6645.

        Test Description: Validate Online extend with mdtest application.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,osa_extend,online_extend
        :avocado: tags=OSAOnlineExtend,test_osa_online_extend_mdtest
        """
        self.log.info("Online Extend : Mdtest")
        self.run_online_extend_test(1, app_name="mdtest")

    def test_osa_online_extend_with_aggregation(self):
        """Test ID: DAOS-6645.

        Test Description: Validate Online extend with aggregation on.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,osa_extend,online_extend
        :avocado: tags=OSAOnlineExtend,test_osa_online_extend_with_aggregation
        """
        self.log.info("Online Extend : Aggregation")
        self.test_during_aggregation = self.params.get("test_with_aggregation",
                                                       '/run/aggregation/*')
        self.run_online_extend_test(1)

    def test_osa_online_extend_exclude_during_rebuild(self):
        """Test ID: DAOS-14441.

        Test Description: Validate Online extend after rebuild is started
        and a rank is excluded.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,osa_extend,online_extend
        :avocado: tags=OSAOnlineExtend,test_osa_online_extend_exclude_during_rebuild
        """
        self.log.info("Online Extend Testing: Exclude during Rebuild")
        self.run_online_extend_test(1, exclude_or_drain="exclude")

    def test_osa_online_extend_drain_after_rebuild(self):
        """Test ID: DAOS-14441.

        Test Description: Validate Online extend after rebuild is completed
        and a rank is drained.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,osa_extend,online_extend
        :avocado: tags=OSAOnlineExtend,test_osa_online_extend_drain_after_rebuild
        """
        self.log.info("Online Extend Testing: Drain after rebuild")
        self.run_online_extend_test(1, exclude_or_drain="drain")
