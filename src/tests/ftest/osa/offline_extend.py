"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from time import sleep
from osa_utils import OSAUtils
from test_utils_pool import add_pool
from dmg_utils import check_system_query_status


class OSAOfflineExtend(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline extend test cases.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        # Start an additional server.
        self.ior_test_sequence = self.params.get("ior_test_sequence", "/run/ior/iorflags/*")
        self.extra_servers = self.get_hosts_from_yaml(
            "test_servers", "server_partition", "server_reservation", "/run/extra_servers/*")
        self.rank = self.params.get("rank_list", "/run/test_ranks/*")
        self.test_oclass = None
        self.dmg_command.exit_status_exception = True

    def run_offline_extend_test(self, num_pool, data=False, oclass=None,
                                exclude_or_drain=None):
        """Run the offline extend without data.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
            oclass (list) : list of daos object class (eg: "RP_2G8")
            exclude_or_drain (str): Pass "exclude" or "drain" string. Defaults to None.
        """
        # Create a pool
        pool = {}
        if oclass is None:
            oclass = []
            oclass.append(self.ior_cmd.dfs_oclass.value)

        self.log.info(oclass[0])

        for val in range(0, num_pool):
            # Perform IOR write using the oclass list
            if val < len(oclass):
                index = val
            else:
                index = 0
            pool[val] = add_pool(self, connect=False)
            self.pool = pool[val]
            test_seq = self.ior_test_sequence[0]
            self.pool.set_property("reclaim", "disabled")
            if data:
                self.run_ior_thread("Write", oclass[index], test_seq)
                self.run_mdtest_thread(oclass[index])
                if self.test_during_aggregation is True:
                    self.run_ior_thread("Write", oclass[index], test_seq)
                if self.test_with_snapshot is True:
                    # Create a snapshot of the container
                    # after IOR job completes.
                    self.container.create_snap()
                    self.log.info("Created container snapshot: %s", self.container.epoch)
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

        for rank_index, rank_val in enumerate(self.rank):
            # If total pools less than 3, extend only a single pool.
            # If total pools >= 3  : Extend only 3 pools.
            if num_pool >= len(self.rank):
                val = rank_index
            else:
                val = 0
            self.pool = pool[val]
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.pool.get_version(True)
            self.log.info("Pool Version at the beginning %s", pver_begin)
            # Get initial total free space (scm+nvme)
            initial_free_space = self.pool.get_total_free_space(refresh=True)
            # Enable aggregation for multiple pool testing only.
            if self.test_during_aggregation is True and (num_pool > 1):
                self.delete_extra_container(self.pool)
            output = self.pool.extend(rank_val)
            self.log.info(output)
            if exclude_or_drain == "exclude":
                self.pool.wait_for_rebuild_to_start()
                # Give a 4 second delay so that some objects are moved
                # as part of rebuild operation.
                sleep(4)
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
            self.log.info("Pool Version after extend %d", pver_extend)
            # Check pool version incremented after pool extend
            self.assertTrue(pver_extend > pver_begin, "Pool Version Error:  After extend")
            display_string = "Pool{} space at the End".format(val)
            pool[val].display_pool_daos_space(display_string)
            self.assertTrue(free_space_after_extend > initial_free_space,
                            "Expected free space after extend is less than initial")

            if data:
                # Perform the IOR read using the same
                # daos object class used for write.
                if val < len(oclass):
                    index = val
                else:
                    index = 0
                self.run_ior_thread("Read", oclass[index], test_seq)
                self.run_mdtest_thread(oclass[index])
                self.container = self.pool_cont_dict[self.pool][0]
                self.container.check()

    def test_osa_offline_extend(self):
        """JIRA ID: DAOS-4751.

        Test Description: Validate Offline Extend

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,osa_extend,offline_extend
        :avocado: tags=OSAOfflineExtend,test_osa_offline_extend
        """
        self.log.info("Offline Extend Testing : With Checksum")
        self.run_offline_extend_test(1, True)

    def test_osa_offline_extend_without_checksum(self):
        """Test ID: DAOS-6924.

        Test Description: Validate Offline extend without Checksum.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,osa_extend,offline_extend
        :avocado: tags=OSAOfflineExtend,test_osa_offline_extend_without_checksum
        """
        self.test_with_checksum = self.params.get("test_with_checksum", '/run/checksum/*')
        self.log.info("Offline Extend Testing: Without Checksum")
        self.run_offline_extend_test(1, data=True)

    def test_osa_offline_extend_multiple_pools(self):
        """Test ID: DAOS-6924.

        Test Description: Validate Offline extend without Checksum.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,osa_extend,offline_extend
        :avocado: tags=OSAOfflineExtend,test_osa_offline_extend_multiple_pools
        """
        self.log.info("Offline Extend Testing: Multiple Pools")
        self.run_offline_extend_test(5, data=True)

    def test_osa_offline_extend_oclass(self):
        """Test ID: DAOS-6924.

        Test Description: Validate Offline extend without Checksum.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,osa_extend,offline_extend
        :avocado: tags=OSAOfflineExtend,test_osa_offline_extend_oclass
        """
        self.log.info("Offline Extend Testing: oclass")
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        self.run_offline_extend_test(4, data=True, oclass=self.test_oclass)

    def test_osa_offline_extend_during_aggregation(self):
        """Test ID: DAOS-6294.

        Test Description: Extend rank while aggregation is happening in parallel

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,osa_extend,offline_extend
        :avocado: tags=OSAOfflineExtend,test_osa_offline_extend_during_aggregation
        """
        self.test_during_aggregation = self.params.get("test_with_aggregation",
                                                       '/run/aggregation/*')
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        self.log.info("Offline Extend : Aggregation")
        self.run_offline_extend_test(3, data=True, oclass=self.test_oclass)

    def test_osa_offline_extend_after_snapshot(self):
        """Test ID: DAOS-8057.

        Test Description: Validate Offline extend after taking snapshot.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,osa_extend,offline_extend
        :avocado: tags=OSAOfflineExtend,test_osa_offline_extend_after_snapshot
        """
        self.test_with_snapshot = self.params.get("test_with_snapshot", '/run/snapshot/*')
        self.log.info("Offline Extend Testing: After taking snapshot")
        self.run_offline_extend_test(1, data=True)

    def test_osa_offline_extend_exclude_during_rebuild(self):
        """Test ID: DAOS-14441.

        Test Description: Validate Offline extend after rebuild is started
        and a rank is excluded.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,osa_extend,offline_extend
        :avocado: tags=OSAOfflineExtend,test_osa_offline_extend_exclude_during_rebuild
        """
        self.log.info("Offline Extend Testing: Exclude during Rebuild")
        self.run_offline_extend_test(1, data=True, exclude_or_drain="exclude")

    def test_osa_offline_extend_drain_after_rebuild(self):
        """Test ID: DAOS-14441.

        Test Description: Validate Offline extend after rebuild is started
        and a rank is drained.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,osa_extend,offline_extend
        :avocado: tags=OSAOfflineExtend,test_osa_offline_extend_drain_after_rebuild
        """
        self.log.info("Offline Extend Testing: Drain after rebuild")
        self.run_offline_extend_test(1, data=True, exclude_or_drain="drain")
