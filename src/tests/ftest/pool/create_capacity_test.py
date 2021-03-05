#!/usr/bin/python
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from pool_test_base import PoolTestBase
from server_utils import ServerFailed


class PoolCreateTests(PoolTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Pool create tests.

    All of the tests verify pool create performance with 7 servers and 1 client.
    Each server should be configured with full compliment of NVDIMMs and SSDs.

    :avocado: recursive
    """

    def test_create_pool_quantity(self):
        """JIRA ID: DAOS-5114 / SRS-2 / SRS-4.

        Test Description:
            Create 200 pools on all of the servers.
            Perform an orderly system shutdown via cmd line (dmg).
            Restart the system via cmd line tool (dmg).
            Verify that DAOS is ready to accept requests with in 2 minutes.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=pool
        :avocado: tags=pool_create_tests,create_performance
        """
        # Create some number of pools each using a equal amount of 60% of the
        # available capacity, e.g. 0.6% for 100 pools.
        quantity = self.params.get("quantity", "/run/pool/*", 1)
        ratio = 0.6 / quantity
        self.pool = self.get_pool_list(quantity, ratio, ratio, 1)
        self.check_pool_creation(3)

        # Verify DAOS can be restarted in less than 2 minutes
        try:
            self.server_managers[0].system_stop()
        except ServerFailed as error:
            self.fail(error)

        start = float(time.time())
        try:
            self.server_managers[0].system_start()
        except ServerFailed as error:
            self.fail(error)

        duration = float(time.time()) - start
        self.assertLessEqual(
            duration, 120,
            "DAOS not ready to accept requests with in 2 minutes")

        # Verify all the pools exists after the restart
        detected_pools = [uuid.lower() for uuid in self.dmg.pool_list()]
        missing_pools = []
        for pool in self.pool:
            pool_uuid = pool.uuid.lower()
            if pool_uuid not in detected_pools:
                missing_pools.append(pool_uuid)
        if missing_pools:
            self.fail(
                "The following created pools were not detected in the pool "
                "list after rebooting the servers:\n  [{}]: {}".format(
                    len(missing_pools), ", ".join(missing_pools)))
        self.assertEqual(
            len(self.pool), len(detected_pools),
            "Additional pools detected after rebooting the servers")
