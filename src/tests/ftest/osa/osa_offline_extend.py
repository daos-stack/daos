#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from osa_utils import OSAUtils
from test_utils_pool import TestPool


class OSAOfflineExtend(OSAUtils):
    # pylint: disable=too-many-ancestors
    """
    Test Class Description: This test runs
    daos_server offline extend test cases.

    :avocado: recursive
    """
    def setUp(self):
        """Set up for test case."""
        super(OSAOfflineExtend, self).setUp()
        self.dmg_command = self.get_dmg_command()
        # Start an additional server.
        self.extra_servers = self.params.get("test_servers",
                                             "/run/extra_servers/*")

    def run_offline_extend_test(self, num_pool, data=False):
        """Run the offline extend without data.
            Args:
            num_pool (int) : total pools to create for testing purposes.
            data (bool) : whether pool has no data or to create
                          some data in pool. Defaults to False.
        """
        # Create a pool
        pool = {}
        pool_uuid = []

        # Extend a ranks 4 and 5
        rank = [4, 5]

        for val in range(0, num_pool):
            pool[val] = TestPool(self.context, dmg_command=self.dmg_command)
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
                self.write_single_object()
        # Start the additional servers and extend the pool
        self.log.info("Extra Servers = %s", self.extra_servers)
        self.start_additional_servers(self.extra_servers)
        # Give sometime for the additional server to come up.
        time.sleep(5)

        # Extend the pool_uuid, rank and targets
        for val in range(0, num_pool):
            self.pool = pool[val]
            scm_size = self.pool.scm_size
            nvme_size = self.pool.nvme_size
            self.pool.display_pool_daos_space("Pool space: Beginning")
            pver_begin = self.get_pool_version()
            self.log.info("Pool Version at the beginning %s", pver_begin)
            output = self.dmg_command.pool_extend(self.pool.uuid,
                                                  rank, scm_size,
                                                  nvme_size)
            self.log.info(output)

            pver_extend = self.get_pool_version()
            fail_count = 0
            while fail_count <= 20:
                pver_extend = self.get_pool_version()
                time.sleep(15)
                fail_count += 1
                if pver_extend > pver_begin:
                    break

            self.assert_on_rebuild_failure()

            pver_extend = self.get_pool_version()
            self.log.info("Pool Version after extend %d", pver_extend)
            # Check pool version incremented after pool extend
            self.assertTrue(pver_extend > pver_begin,
                            "Pool Version Error:  After extend")

        for val in range(0, num_pool):
            display_string = "Pool{} space at the End".format(val)
            pool[val].display_pool_daos_space(display_string)

        if data:
            self.verify_single_object()

    def test_osa_offline_extend(self):
        """
        JIRA ID: DAOS-4751

        Test Description: Validate Offline Extend

        :avocado: tags=all,daily_regression,hw,medium,ib2
        :avocado: tags=osa,osa_extend,offline_extend
        """
        # Perform extend testing with 1 pool
        self.run_offline_extend_test(1, True)
