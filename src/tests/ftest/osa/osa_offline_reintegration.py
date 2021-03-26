#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random
from osa_utils import OSAUtils
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
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_test_sequence = self.params.get(
            "ior_test_sequence", '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get(
            "obj_class", '/run/ior/iorflags/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)

    def run_offline_reintegration_test(self, num_pool, data=False,
                                       server_boot=False):
        """Run the offline reintegration without data.

        Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                some data in pool. Defaults to False.
            server_boot (bool) : Perform system stop/start on a rank.
                Defaults to False.
        """
        # Create a pool
        pool = {}
        pool_uuid = []
        exclude_servers = (len(self.hostlist_servers) * 2) - 1

        # Exclude rank : two ranks other than rank 0.
        rank = random.randint(1, exclude_servers)

        for val in range(0, num_pool):
            pool[val] = TestPool(self.context,
                                 dmg_command=self.get_dmg_command())
            pool[val].get_params(self)
            # Split total SCM and NVME size for creating multiple pools.
            pool[val].scm_size.value = int(pool[val].scm_size.value /
                                           num_pool)
            pool[val].nvme_size.value = int(pool[val].nvme_size.value /
                                            num_pool)
            pool[val].create()
            pool_uuid.append(pool[val].uuid)
            self.pool = pool[val]
            if data:
                self.run_ior_thread("Write", self.ior_dfs_oclass[0],
                                    self.ior_apis[0], self.ior_test_sequence[0])

        # Exclude and reintegrate the pool_uuid, rank and targets
        for val in range(0, num_pool):
            self.pool = pool[val]
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            if server_boot is False:
                output = self.dmg_command.pool_exclude(self.pool.uuid,
                                                       rank)
            else:
                output = self.dmg_command.system_stop(ranks=rank)
                self.pool.wait_for_rebuild(True)
                self.log.info(output)
                output = self.dmg_command.system_start(ranks=rank)

            self.log.info(output)
            self.is_rebuild_done(3)
            self.assert_on_rebuild_failure()

            pver_exclude = self.get_pool_version()
            self.log.info("Pool Version after exclude %s", pver_exclude)
            # Check pool version incremented after pool exclude
            # pver_exclude should be greater than
            # pver_begin + 8 targets.
            self.assertTrue(pver_exclude > (pver_begin + 8),
                            "Pool Version Error:  After exclude")
            output = self.dmg_command.pool_reintegrate(self.pool.uuid,
                                                       rank)
            self.log.info(output)
            self.is_rebuild_done(3)
            self.assert_on_rebuild_failure()

            pver_reint = self.get_pool_version()
            self.log.info("Pool Version after reintegrate %d", pver_reint)
            # Check pool version incremented after pool reintegrate
            self.assertTrue(pver_reint > (pver_exclude + 1),
                            "Pool Version Error:  After reintegrate")

        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            self.pool = pool[val]
            self.pool.display_pool_daos_space(display_string)

        if data:
            self.run_ior_thread("Read", self.ior_dfs_oclass[0],
                                self.ior_apis[0], self.ior_test_sequence[0])

    def test_osa_offline_reintegration(self):
        """Test ID: DAOS-4749.

        Test Description: Validate Offline Reintegration

        :avocado: tags=all,daily_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration
        """
        # Perform reintegration testing with a pool
        self.run_offline_reintegration_test(1, True)

    @skipForTicket("DAOS-6766, DAOS-6783")
    def test_osa_offline_reintegration_server_stop(self):
        """Test ID: DAOS-6748.

        Test Description: Validate Offline Reintegration with server stop

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,osa
        :avocado: tags=offline_reintegration_srv_stop
        """
        self.run_offline_reintegration_test(1, data=True, server_boot=True)
