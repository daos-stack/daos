#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random
from osa_utils import OSAUtils
from test_utils_pool import TestPool
from write_host_file import write_host_file


class OSAOfflineDrain(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline drain test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super().setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_test_sequence = self.params.get(
            "ior_test_sequence", '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)

    def run_offline_drain_test(self, num_pool, data=False,
                               oclass=None):
        """Run the offline drain without data.
            Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
            oclass (str): DAOS object class (eg: RP_2G1,etc)
        """
        # Create a pool
        pool = {}
        target_list = []

        if oclass is None:
            oclass = self.ior_cmd.dfs_oclass.value

        # Exclude target : random two targets  (target idx : 0-7)
        n = random.randint(0, 6)
        target_list.append(n)
        target_list.append(n+1)
        t_string = "{},{}".format(target_list[0], target_list[1])

        # Drain a rank 1 (or server)
        rank = 1

        for val in range(0, num_pool):
            pool[val] = TestPool(self.context, dmg_command=self.dmg_command)
            pool[val].get_params(self)
            # Split total SCM and NVME size for creating multiple pools.
            pool[val].scm_size.value = int(pool[val].scm_size.value /
                                           num_pool)
            pool[val].nvme_size.value = int(pool[val].nvme_size.value /
                                            num_pool)
            pool[val].create()
            self.pool = pool[val]
            self.pool.set_property("reclaim", "disabled")
            test_seq = self.ior_test_sequence[0]

            if data:
                self.run_ior_thread("Write", oclass, test_seq)
                self.run_mdtest_thread()

        # Drain rank and targets
        for val in range(0, num_pool):
            self.pool = pool[val]
            rank = rank + val
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            if self.test_during_aggregation is True:
                self.pool.set_property("reclaim", "time")
                self.delete_extra_container(self.pool)
                self.simple_exclude_reintegrate_loop(rank)
            output = self.dmg_command.pool_drain(self.pool.uuid,
                                                 rank, t_string)
            self.print_and_assert_on_rebuild_failure(output)

            pver_drain = self.get_pool_version()
            self.log.info("Pool Version after drain %d", pver_drain)
            # Check pool version incremented after pool drain
            self.assertTrue(pver_drain > (pver_begin + 1),
                            "Pool Version Error:  After drain")

        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            pool[val].display_pool_daos_space(display_string)

        if data:
            self.run_ior_thread("Read", oclass, test_seq)
            self.run_mdtest_thread()

    def test_osa_offline_drain(self):
        """
        JIRA ID: DAOS-4750

        Test Description: Validate Offline Drain

        :avocado: tags=all,daily_regression,hw,medium,ib2
        :avocado: tags=osa,osa_drain,offline_drain
        """
        self.run_offline_drain_test(1, True)
