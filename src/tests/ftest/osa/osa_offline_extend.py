#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from osa_utils import OSAUtils
from daos_utils import DaosCommand
from test_utils_pool import TestPool, LabelGenerator
from dmg_utils import check_system_query_status
from apricot import skipForTicket


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
        self.daos_command = DaosCommand(self.bin)
        # Start an additional server.
        self.ior_test_sequence = self.params.get("ior_test_sequence",
                                                 '/run/ior/iorflags/*')
        self.extra_servers = self.params.get("test_servers",
                                             "/run/extra_servers/*")
        self.rank = self.params.get("rank_list", '/run/test_ranks/*')
        self.test_oclass = None
        self.dmg_command.exit_status_exception = True

    def run_offline_extend_test(self, num_pool, data=False,
                                oclass=None):
        """Run the offline extend without data.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
            oclass (list) : list of daos object class (eg: "RP_2G8")
        """
        # Create a pool
        label_generator = LabelGenerator()
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
            pool[val] = TestPool(
                context=self.context, dmg_command=self.get_dmg_command(),
                label_generator=label_generator)
            pool[val].get_params(self)
            pool[val].create()
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
                    self.log.info("Created container snapshot: %s",
                                  self.container.epoch)
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
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            # Enable aggregation for multiple pool testing only.
            if self.test_during_aggregation is True and (num_pool > 1):
                self.delete_extra_container(self.pool)
            output = self.dmg_command.pool_extend(self.pool.uuid, rank_val)
            self.print_and_assert_on_rebuild_failure(output)

            pver_extend = self.get_pool_version()
            self.log.info("Pool Version after extend %d", pver_extend)
            # Check pool version incremented after pool extend
            self.assertTrue(pver_extend > pver_begin,
                            "Pool Version Error:  After extend")

            display_string = "Pool{} space at the End".format(val)
            pool[val].display_pool_daos_space(display_string)

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
                kwargs = {"pool": self.pool.uuid,
                          "cont": self.container.uuid}
                output = self.daos_command.container_check(**kwargs)
                self.log.info(output)

    def test_osa_offline_extend(self):
        """
        JIRA ID: DAOS-4751

        Test Description: Validate Offline Extend

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=osa,checksum,osa_extend
        :avocado: tags=offline_extend,offline_extend_with_csum
        """
        self.log.info("Offline Extend Testing : With Checksum")
        self.run_offline_extend_test(1, True)

    def test_osa_offline_extend_without_checksum(self):
        """Test ID: DAOS-6924
        Test Description: Validate Offline extend without
        Checksum.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=osa,osa_extend
        :avocado: tags=offline_extend,offline_extend_without_csum
        """
        self.test_with_checksum = self.params.get("test_with_checksum",
                                                  '/run/checksum/*')
        self.log.info("Offline Extend Testing: Without Checksum")
        self.run_offline_extend_test(1, data=True)

    def test_osa_offline_extend_multiple_pools(self):
        """Test ID: DAOS-6924
        Test Description: Validate Offline extend without
        Checksum.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=osa,osa_extend
        :avocado: tags=offline_extend,offline_extend_multiple_pools
        """
        self.log.info("Offline Extend Testing: Multiple Pools")
        self.run_offline_extend_test(5, data=True)

    @skipForTicket("DAOS-7493")
    def test_osa_offline_extend_oclass(self):
        """Test ID: DAOS-6924
        Test Description: Validate Offline extend without
        Checksum.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=osa,osa_extend
        :avocado: tags=offline_extend,offline_extend_oclass
        """
        self.log.info("Offline Extend Testing: oclass")
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        self.run_offline_extend_test(4, data=True,
                                     oclass=self.test_oclass)

    @skipForTicket("DAOS-7195")
    def test_osa_offline_extend_during_aggregation(self):
        """Test ID: DAOS-6294
        Test Description: Extend rank while aggregation
        is happening in parallel

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=osa,checksum,osa_extend
        :avocado: tags=offline_extend,offline_extend_during_aggregation
        """
        self.test_during_aggregation = self.params.get("test_with_aggregation",
                                                       '/run/aggregation/*')
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        self.log.info("Offline Extend : Aggregation")
        self.run_offline_extend_test(3, data=True, oclass=self.test_oclass)

    def test_osa_offline_extend_after_snapshot(self):
        """Test ID: DAOS-8057
        Test Description: Validate Offline extend after
        taking snapshot.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=osa,osa_extend
        :avocado: tags=offline_extend,offline_extend_after_snapshot
        """
        self.test_with_snapshot = self.params.get("test_with_snapshot",
                                                  '/run/snapshot/*')
        self.log.info("Offline Extend Testing: After taking snapshot")
        self.run_offline_extend_test(1, data=True)
