"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from nvme_utils import ServerFillUp
from osa_utils import OSAUtils
from test_utils_pool import add_pool
from write_host_file import write_host_file


class OSAOfflineReintegration(OSAUtils, ServerFillUp):
    # pylint: disable=too-many-ancestors
    """OSA offline Reintegration test cases.

    Test Class Description:
        This test runs daos_server offline reintegration test cases.

    :avocado: recursive
    """

    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_test_sequence = self.params.get("ior_test_sequence", '/run/ior/iorflags/*')
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        self.ior_test_repetitions = self.params.get("pool_test_repetitions", '/run/pool_capacity/*')
        self.loop_test_cnt = 1
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(self.hostlist_clients, self.workdir)
        self.dmg_command.exit_status_exception = True

    def run_offline_reintegration_test(self, num_pool, data=False, server_boot=False, oclass=None,
                                       pool_fillup=0):
        # pylint: disable=too-many-branches
        """Run the offline reintegration without data.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create some data in pool. Defaults to
                False.
            server_boot (bool) : Perform system stop/start on a rank. Defaults to False.
            oclass (str) : daos object class string (eg: "RP_2G8")
            pool_fillup (int) : Percentage of pool filled up with data before performing OSA
                                operations.
        """
        # Create 'num_pool' number of pools
        pools = []
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value
        self.log.info("==> Creating %s pools with oclass: %s", num_pool, oclass)
        for index in range(0, num_pool):
            pools.append(add_pool(self, connect=False))
            self.pool = pools[-1]
            self.pool.set_property("reclaim", "disabled")
            test_seq = self.ior_test_sequence[0]
            if data:
                # if pool_fillup is greater than 0, then
                # use start_ior_load method from nvme_utils.py.
                # Otherwise, use the osa_utils.py run_ior_thread
                # method.
                if pool_fillup > 0:
                    self.ior_cmd.dfs_oclass.update(oclass)
                    self.ior_cmd.dfs_dir_oclass.update(oclass)
                    self.ior_default_flags = self.ior_w_flags
                    self.ior_cmd.repetitions.update(self.ior_test_repetitions)
                    self.log.info(self.pool.pool_percentage_used())
                    self.start_ior_load(storage='NVMe', operation="Auto_Write", percent=pool_fillup)
                    self.log.info(self.pool.pool_percentage_used())
                else:
                    self.run_ior_thread("Write", oclass, test_seq)
                    self.run_mdtest_thread(oclass)
                if self.test_with_snapshot is True:
                    # Create a snapshot of the container
                    # after IOR job completes.
                    self.container.create_snap()
                    self.log.info("Created container snapshot: %s", self.container.epoch)
                if self.test_during_aggregation is True:
                    self.run_ior_thread("Write", oclass, test_seq)

        # Exclude ranks 0 and 3 from a random pool
        ranks = [0, 3]
        self.pool = self.random.choice(pools)
        for loop in range(0, self.loop_test_cnt):
            self.log.info(
                "==> (Loop %s/%s) Excluding ranks %s from %s",
                loop + 1, self.loop_test_cnt, ranks, str(self.pool))
            for index, rank in enumerate(ranks):
                self.pool.display_pool_daos_space("Pool space: Beginning")
                pver_begin = self.pool.get_version(True)
                self.log.info("Pool Version at the beginning %s", pver_begin)
                # Get initial total free space (scm+nvme)
                initial_free_space = self.pool.get_total_free_space(refresh=True)
                if server_boot is False:
                    if (self.test_during_rebuild is True and index == 0):
                        # Exclude rank 5
                        output = self.pool.exclude("5")
                        self.print_and_assert_on_rebuild_failure(output)
                    if self.test_during_aggregation is True:
                        self.delete_extra_container(self.pool)
                        self.simple_osa_reintegrate_loop(rank)
                    # For redundancy factor testing, just exclude only
                    # one target on a rank. Don't exclude a rank(s).
                    if (self.test_with_rf is True and index == 0):
                        output = self.pool.exclude(rank)
                    elif (self.test_with_rf is True and index > 0):
                        continue
                    else:
                        if pool_fillup > 0 and index > 0:
                            continue
                        output = self.pool.exclude(rank)
                else:
                    output = self.dmg_command.system_stop(ranks=rank, force=True)
                    self.print_and_assert_on_rebuild_failure(output)
                    output = self.dmg_command.system_start(ranks=rank)
                # Just try to reintegrate rank 5
                if (self.test_during_rebuild is True and index == 2):
                    # Reintegrate rank 5
                    output = self.pool.reintegrate("5")
                self.print_and_assert_on_rebuild_failure(output)

                pver_exclude = self.pool.get_version(True)
                self.log.info("Pool Version after exclude %s", pver_exclude)
                free_space_after_exclude = self.pool.get_total_free_space(refresh=True)
                # Check pool version incremented after pool exclude
                # pver_exclude should be greater than
                # pver_begin + 1 (1 target + exclude)
                self.assertTrue(pver_exclude > (pver_begin + 1),
                                "Pool Version Error: After exclude")
                self.assertTrue(initial_free_space > free_space_after_exclude,
                                "Expected free space after exclude is less than initial")

            # Reintegrate the ranks which was excluded
            self.log.info(
                "==> (Loop %s/%s) Reintegrating ranks %s into %s",
                loop + 1, self.loop_test_cnt, ranks, str(self.pool))
            for index, rank in enumerate(ranks):
                if self.test_with_blank_node is True:
                    ip_addr, p_num = self.get_ipaddr_for_rank(rank)
                    self.remove_pool_dir(ip_addr, p_num)
                if (index == 2 and "RP_2G" in oclass):
                    output = self.pool.reintegrate(rank, "0,2")
                elif (self.test_with_rf is True and index == 0):
                    output = self.pool.reintegrate(rank)
                elif (self.test_with_rf is True and index > 0):
                    continue
                else:
                    if pool_fillup > 0 and index > 0:
                        continue
                    output = self.pool.reintegrate(rank)
                self.print_and_assert_on_rebuild_failure(output, timeout=15)
                free_space_after_reintegration = self.pool.get_total_free_space(refresh=True)
                pver_reint = self.pool.get_version(True)
                self.log.info("Pool Version after reintegrate %d", pver_reint)
                # Check pool version incremented after pool reintegrate
                self.assertTrue(pver_reint > pver_exclude, "Pool Version Error:  After reintegrate")
                self.assertTrue(free_space_after_reintegration > free_space_after_exclude,
                                "Expected free space after reintegration is less than exclude")

            display_string = "{} space at the End".format(str(self.pool))
            self.pool.display_pool_daos_space(display_string)

        # Finally check whether the written data can be accessed.
        # Also, run the daos cont check (for object integrity)
        for pool in pools:
            self.pool = pool
            if data:
                if pool_fillup > 0:
                    self.start_ior_load(storage='NVMe', operation='Auto_Read', percent=pool_fillup)
                else:
                    self.run_ior_thread("Read", oclass, test_seq)
                    self.run_mdtest_thread(oclass)
                    self.container = self.pool_cont_dict[self.pool][0]
                    self.container.check()

    def test_osa_offline_reintegration_without_checksum(self):
        """Test ID: DAOS-6923.

        Test Description: Validate Offline Reintegration without enabling checksum in container
            properties.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,offline_reintegration,ior
        :avocado: tags=OSAOfflineReintegration,test_osa_offline_reintegration_without_checksum
        """
        self.test_with_checksum = self.params.get("test_with_checksum", '/run/checksum/*')
        self.log.info("Offline Reintegration : Without Checksum")
        self.run_offline_reintegration_test(1, data=True)

    def test_osa_offline_reintegration_multiple_pools(self):
        """Test ID: DAOS-6923.

        Test Description: Validate Offline Reintegration with multiple pools

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,offline_reintegration,ior
        :avocado: tags=OSAOfflineReintegration,test_osa_offline_reintegration_multiple_pools
        """
        self.log.info("Offline Reintegration : Multiple Pools")
        self.run_offline_reintegration_test(5, data=True)

    def test_osa_offline_reintegration_server_stop(self):
        """Test ID: DAOS-6748.

        Test Description: Validate Offline Reintegration with server stop

        :avocado: tags=all,pr,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,offline_reintegration,ior
        :avocado: tags=OSAOfflineReintegration,test_osa_offline_reintegration_server_stop
        """
        self.log.info("Offline Reintegration : System Start/Stop")
        self.run_offline_reintegration_test(1, data=True, server_boot=True)

    def test_osa_offline_reintegrate_during_rebuild(self):
        """Test ID: DAOS-6923.

        Test Description: Reintegrate rank while rebuild is happening in parallel

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,offline_reintegration,ior
        :avocado: tags=OSAOfflineReintegration,test_osa_offline_reintegrate_during_rebuild
        """
        self.loop_test_cnt = self.params.get("iterations", '/run/loop_test/*')
        self.test_during_rebuild = self.params.get("test_with_rebuild", '/run/rebuild/*')
        self.log.info("Offline Reintegration : Rebuild")
        self.run_offline_reintegration_test(1, data=True)

    def test_osa_offline_reintegration_oclass(self):
        """Test ID: DAOS-6923.

        Test Description: Validate Offline Reintegration with different object class

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,offline_reintegration,ior
        :avocado: tags=OSAOfflineReintegration,test_osa_offline_reintegration_oclass
        """
        self.log.info("Offline Reintegration : Object Class")
        for oclass in self.test_oclass:
            self.run_offline_reintegration_test(1, data=True, server_boot=False, oclass=oclass)

    def test_osa_offline_reintegrate_during_aggregation(self):
        """Test ID: DAOS-6923.

        Test Description: Reintegrate rank while aggregation is happening in parallel

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,offline_reintegration,ior
        :avocado: tags=OSAOfflineReintegration,test_osa_offline_reintegrate_during_aggregation
        """
        self.test_during_aggregation = self.params.get("test_with_aggregation",
                                                       '/run/aggregation/*')
        self.log.info("Offline Reintegration : Aggregation")
        self.run_offline_reintegration_test(1, data=True)

    def test_osa_offline_reintegration_with_rf(self):
        """Test ID: DAOS-6923.

        Test Description: Validate Offline Reintegration with just redundancy factor setting.
        Don't set the oclass during ior run.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,checksum,offline_reintegration,mpich
        :avocado: tags=OSAOfflineReintegration,test_osa_offline_reintegration_with_rf
        """
        self.log.info("Offline Reintegration : RF")
        self.test_with_rf = self.params.get("test_with_rf", '/run/test_rf/*')
        self.run_offline_reintegration_test(1, data=True)

    def test_osa_offline_reintegrate_with_blank_node(self):
        """Test ID: DAOS-6923.

        Test Description: Reintegrate rank with no data.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,offline_reintegration
        :avocado: tags=OSAOfflineReintegration,test_osa_offline_reintegrate_with_blank_node
        """
        self.test_with_blank_node = self.params.get("test_with_blank_node", '/run/blank_node/*')
        self.log.info("Offline Reintegration : Test with blank node")
        self.run_offline_reintegration_test(1, data=True)

    def test_osa_offline_reintegrate_after_snapshot(self):
        """Test ID: DAOS-8057.

        Test Description: Reintegrate rank after taking snapshot.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,offline_reintegration
        :avocado: tags=OSAOfflineReintegration,test_osa_offline_reintegrate_after_snapshot
        """
        self.test_with_snapshot = self.params.get("test_with_snapshot", '/run/snapshot/*')
        self.log.info("Offline Reintegration : Test with snapshot")
        self.run_offline_reintegration_test(1, data=True)

    def test_osa_offline_reintegrate_with_less_pool_space(self):
        """Test ID: DAOS-7160.

        Test Description: Reintegrate rank will less pool space.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=osa,offline_reintegration
        :avocado: tags=OSAOfflineReintegration,test_osa_offline_reintegrate_with_less_pool_space
        """
        self.log.info("Offline Reintegration : Test with less pool space")
        oclass = self.params.get("pool_test_oclass", '/run/pool_capacity/*')
        pool_fillup = self.params.get("pool_fillup", '/run/pool_capacity/*')
        self.run_offline_reintegration_test(1, data=True, oclass=oclass, pool_fillup=pool_fillup)
