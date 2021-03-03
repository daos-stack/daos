#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import random
import threading
from osa_utils import OSAUtils
from test_utils_pool import TestPool
from write_host_file import write_host_file
from apricot import skipForTicket


class OSAOfflineReintegration(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline reintegration test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(OSAOfflineReintegration, self).setUp()
        self.dmg_command = self.get_dmg_command()
        self.ior_apis = self.params.get("ior_api", '/run/ior/iorflags/*')
        self.ior_test_sequence = self.params.get(
            "ior_test_sequence", '/run/ior/iorflags/*')
        self.ior_dfs_oclass = self.params.get(
            "obj_class", '/run/ior/iorflags/*')
        self.test_oclass = self.params.get("oclass", '/run/test_obj_class/*')
        # Recreate the client hostfile without slots defined
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, None)

    def run_offline_reintegration_test(self, num_pool, data=False,
                                       server_boot=False, oclass=None):
        """Run the offline reintegration without data.
            Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
            server_boot (bool) : Perform system stop/start on a rank.
                                 Defults to False.
            oclass (str) : daos object class string (eg: "RP_2G8")
        """
        # Create a pool
        pool = {}
        pool_uuid = []

        # Exclude ranks [0, 3, 4]
        rank = [0, 3, 4]
        if oclass is None:
            oclass = self.ior_dfs_oclass[0]

        for val in range(0, num_pool):
            pool[val] = TestPool(self.context,
                                 dmg_command=self.get_dmg_command())
            pool[val].get_params(self)
            pool[val].create()
            pool_uuid.append(pool[val].uuid)
            self.pool = pool[val]
            if data:
                self.run_ior_thread("Write", oclass,
                                    self.ior_apis[0],
                                    self.ior_test_sequence[0])

        # Exclude all the ranks
        random_pool = random.randint(0, (num_pool-1))
        for val in range(len(rank)):
            self.pool = pool[random_pool]
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            if server_boot is False:
                output = self.dmg_command.pool_exclude(self.pool.uuid,
                                                       rank[val])
            else:
                output = self.dmg_command.system_stop(ranks=rank[val])
                self.pool.wait_for_rebuild(True)
                self.log.info(output)
                output = self.dmg_command.system_start(ranks=rank[val])

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

        # Reintegrate the ranks which was excluded
        for val in range(0, len(rank)):
            if val == 2:
                output = self.dmg_command.pool_reintegrate(self.pool.uuid,
                                                           rank[val], "0,2")
            else:
                output = self.dmg_command.pool_reintegrate(self.pool.uuid,
                                                           rank[val])
            self.log.info(output)
            self.is_rebuild_done(3)
            self.assert_on_rebuild_failure()

            pver_reint = self.get_pool_version()
            self.log.info("Pool Version after reintegrate %d", pver_reint)
            # Check pool version incremented after pool reintegrate
            self.assertTrue(pver_reint > (pver_exclude + 1),
                            "Pool Version Error:  After reintegrate")

        display_string = "Pool{} space at the End".format(random_pool)
        self.pool = pool[random_pool]
        self.pool.display_pool_daos_space(display_string)

        if data:
            self.run_ior_thread("Read", oclass,
                                self.ior_apis[0], self.ior_test_sequence[0])

    def test_osa_offline_reintegration(self):
        """Test ID: DAOS-4749
        Test Description: Validate Offline Reintegration

        :avocado: tags=all,daily_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration
        :avocado: tags=offline_reintegration_exclude
        """
        # Perform reintegration testing with a pool
        self.run_offline_reintegration_test(1, True)

    @skipForTicket("DAOS-6766, DAOS-6783")
    def test_osa_offline_reintegration_server_stop(self):
        """Test ID: DAOS-6748.
        Test Description: Validate Offline Reintegration with server stop
        :avocado: tags=all,pr,daily_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration
        :avocado: tags=offline_reintegration_srv_stop
        """
        self.run_offline_reintegration_test(1, data=True, server_boot=True)

    @skipForTicket("DAOS-6505")
    def test_osa_offline_reintegration_200_pools(self):
        """Test ID: DAOS-6923
        Test Description: Validate Offline Reintegration
        with 200 pools

        :avocado: tags=all,full_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration
        :avocado: tags=offline_reintegration_200
        """
        # Perform reintegration testing with a pool
        self.run_offline_reintegration_test(200, True)

    def test_osa_offline_reintegration_oclass(self):
        """Test ID: DAOS-6923
        Test Description: Validate Offline Reintegration
        with different object class

        :avocado: tags=all,full_regression,hw,medium,ib2
        :avocado: tags=osa,offline_reintegration
        :avocado: tags=offline_reintegration_oclass
        """
        # Perform reintegration testing with a pool
        for oclass in self.test_oclass:
            self.run_offline_reintegration_test(1, data=True,
                                                server_boot=False,
                                                oclass=oclass)
