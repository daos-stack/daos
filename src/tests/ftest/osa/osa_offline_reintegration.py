#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random
from osa_utils import OSAUtils
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from write_host_file import write_host_file
from apricot import skipForTicket


class OSAOfflineReintegration(OSAUtils):
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
        self.daos_command = DaosCommand(self.bin)
        self.ior_test_sequence = self.params.get("ior_test_sequence",
                                                 '/run/ior/iorflags/*')
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        self.loop_test_cnt = 1
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)
        self.dmg_command.exit_status_exception = True

    def run_offline_reintegration_test(self, num_pool, data=False,
                                       server_boot=False, oclass=None):
        """Run the offline reintegration without data.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                some data in pool. Defaults to False.
            server_boot (bool) : Perform system stop/start on a rank.
                                 Defaults to False.
            oclass (str) : daos object class string (eg: "RP_2G8")
        """
        # Create a pool
        pool = {}
        random_pool = 0
        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value

        # Exclude ranks [0, 3, 4]
        rank = [0, 3, 4]
        for val in range(0, num_pool):
            pool[val] = TestPool(self.context,
                                 dmg_command=self.get_dmg_command())
            pool[val].get_params(self)
            pool[val].create()
            self.pool = pool[val]
            self.pool.set_property("reclaim", "disabled")
            test_seq = self.ior_test_sequence[0]
            if data:
                self.run_ior_thread("Write", oclass, test_seq)
                self.run_mdtest_thread()
                if self.test_during_aggregation is True:
                    self.run_ior_thread("Write", oclass, test_seq)

        # Exclude all the ranks
        random_pool = random.randint(0, (num_pool-1))
        for _ in range(0, self.loop_test_cnt):
            for val, _ in enumerate(rank):
                self.pool = pool[random_pool]
                self.pool.display_pool_daos_space("Pool space: Beginning")
                pver_begin = self.get_pool_version()
                self.log.info("Pool Version at the beginning %s", pver_begin)
                if server_boot is False:
                    if (self.test_during_rebuild is True and val == 0):
                        # Exclude rank 5
                        output = self.dmg_command.pool_exclude(self.pool.uuid,
                                                               "5")
                        self.print_and_assert_on_rebuild_failure(output)
                    if self.test_during_aggregation is True:
                        self.delete_extra_container(self.pool)
                        self.simple_exclude_reintegrate_loop(rank[val])
                    output = self.dmg_command.pool_exclude(self.pool.uuid,
                                                           rank[val])
                    # Check the IOR data after exclude
                    if data:
                        self.run_ior_thread("Read", oclass, test_seq)
                else:
                    output = self.dmg_command.system_stop(ranks=rank[val],
                                                          force=True)
                    self.print_and_assert_on_rebuild_failure(output)
                    # Check the IOR data after system stop
                    if data and (val == 0):
                        self.run_ior_thread("Read", oclass, test_seq)
                    output = self.dmg_command.system_start(ranks=rank[val])
                # Just try to reintegrate rank 5
                if (self.test_during_rebuild is True and val == 2):
                    # Reintegrate rank 5
                    output = self.dmg_command.pool_reintegrate(self.pool.uuid,
                                                               "5")
                self.print_and_assert_on_rebuild_failure(output)

                pver_exclude = self.get_pool_version()
                self.log.info("Pool Version after exclude %s", pver_exclude)
                # Check pool version incremented after pool exclude
                # pver_exclude should be greater than
                # pver_begin + 3 (2 targets + exclude)
                self.assertTrue(pver_exclude > (pver_begin + 3),
                                "Pool Version Error: After exclude")

            # Reintegrate the ranks which was excluded
            for val, _ in enumerate(rank):
                if (val == 2 and "RP_2G" in oclass):
                    output = self.dmg_command.pool_reintegrate(self.pool.uuid,
                                                               rank[val],
                                                               "0,2")
                else:
                    output = self.dmg_command.pool_reintegrate(self.pool.uuid,
                                                               rank[val])
                self.print_and_assert_on_rebuild_failure(output)

                pver_reint = self.get_pool_version()
                self.log.info("Pool Version after reintegrate %d", pver_reint)
                # Check pool version incremented after pool reintegrate
                self.assertTrue(pver_reint > (pver_exclude + 1),
                                "Pool Version Error:  After reintegrate")

            display_string = "Pool{} space at the End".format(random_pool)
            self.pool = pool[random_pool]
            self.pool.display_pool_daos_space(display_string)

        # Finally check whether the written data can be accessed.
        # Also, run the daos cont check (for object integrity)
        for val in range(0, num_pool):
            self.pool = pool[val]
            if data:
                self.run_ior_thread("Read", oclass, test_seq)
                self.run_mdtest_thread()
                self.container = self.pool_cont_dict[self.pool][0]
                kwargs = {"pool": self.pool.uuid,
                          "cont": self.container.uuid}
                output = self.daos_command.container_check(**kwargs)
                self.log.info(output)

    def test_osa_offline_reintegration_without_checksum(self):
        """Test ID: DAOS-6923
        Test Description: Validate Offline Reintegration
        without enabling checksum in container properties.

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration_daily
        :avocado: tags=offline_reintegration_without_csum
        """
        self.test_with_checksum = self.params.get("test_with_checksum",
                                                  '/run/checksum/*')
        self.log.info("Offline Reintegration : Without Checksum")
        self.run_offline_reintegration_test(1, data=True)

    def test_osa_offline_reintegration_multiple_pools(self):
        """Test ID: DAOS-6923
        Test Description: Validate Offline Reintegration
        with multiple pools

        :avocado: tags=all,daily_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration_daily
        :avocado: tags=offline_reintegration_multiple_pools
        """
        self.log.info("Offline Reintegration : Multiple Pools")
        self.run_offline_reintegration_test(5, data=True)

    @skipForTicket("DAOS-6807")
    def test_osa_offline_reintegration_server_stop(self):
        """Test ID: DAOS-6748.

        Test Description: Validate Offline Reintegration with server stop
        :avocado: tags=all,pr,daily_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration_daily
        :avocado: tags=offline_reintegration_srv_stop
        """
        self.log.info("Offline Reintegration : System Start/Stop")
        self.run_offline_reintegration_test(1, data=True, server_boot=True)

    def test_osa_offline_reintegrate_during_rebuild(self):
        """Test ID: DAOS-6923
        Test Description: Reintegrate rank while rebuild
        is happening in parallel

        :avocado: tags=all,full_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration_full
        :avocado: tags=offline_reintegrate_during_rebuild
        """
        self.loop_test_cnt = self.params.get("iterations",
                                             '/run/loop_test/*')
        self.test_during_rebuild = self.params.get("test_with_rebuild",
                                                   '/run/rebuild/*')
        self.log.info("Offline Reintegration : Rebuild")
        self.run_offline_reintegration_test(1, data=True)

    def test_osa_offline_reintegration_oclass(self):
        """Test ID: DAOS-6923
        Test Description: Validate Offline Reintegration
        with different object class

        :avocado: tags=all,full_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration_full
        :avocado: tags=offline_reintegration_oclass
        """
        self.log.info("Offline Reintegration : Object Class")
        for oclass in self.test_oclass:
            self.run_offline_reintegration_test(1, data=True,
                                                server_boot=False,
                                                oclass=oclass)

    def test_osa_offline_reintegrate_during_aggregation(self):
        """Test ID: DAOS-6923
        Test Description: Reintegrate rank while aggregation
        is happening in parallel

        :avocado: tags=all,full_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration_full
        :avocado: tags=offline_reintegrate_during_aggregation
        """
        self.test_during_aggregation = self.params.get("test_with_aggregation",
                                                       '/run/aggregation/*')
        self.log.info("Offline Reintegration : Aggregation")
        self.run_offline_reintegration_test(1, data=True)
