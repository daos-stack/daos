"""
(C) Copyright 2021-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from server_utils import ServerFailed
from test_utils_pool import add_pool, check_pool_creation


class PoolCreateCapacityTests(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Pool create tests.

    All of the tests verify pool create performance with 7 servers and 1 client.
    Each server should be configured with full compliment of NVDIMMs and SSDs.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Create test-case-specific DAOS log files
        self.update_log_file_names()
        super().setUp()

    def test_create_pool_quantity(self):
        """JIRA ID: DAOS-5114 / SRS-2 / SRS-4.

        Test Description:
            Create 200 pools on all of the servers.
            Perform an orderly system shutdown via cmd line (dmg).
            Restart the system via cmd line tool (dmg).
            Verify that DAOS is ready to accept requests within 2 minutes.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=PoolCreateCapacityTests,test_create_pool_quantity
        """
        # Create some number of pools each using a equal amount of 60% of the
        # available capacity, e.g. 0.6% for 100 pools.
        quantity = self.params.get("quantity", "/run/pool/*", 1)
        storage = self.server_managers[0].get_available_storage()
        if storage['nvme'] < 750156374016:
            self.log.info(
                'Reducing pool quantity from %s -> 150 due to insufficient NVMe capacity (%s < '
                '750156374016)', quantity, storage['nvme'])
            quantity = 150

        # Define all the pools with the same size defined in the test yaml
        self.log_step('Defining {} pools'.format(quantity))
        pools = []
        for _ in range(quantity):
            pools.append(add_pool(self, create=False))

        # Create all the pools
        self.log_step('Creating {} pools (dmg pool create)'.format(quantity))
        self.get_dmg_command().server_set_logmasks("DEBUG", raise_exception=False)
        check_pool_creation(self, pools, 30, 2)
        self.get_dmg_command().server_set_logmasks(raise_exception=False)

        # Verify DAOS can be restarted in less than 2 minutes
        self.log_step('Stopping all engines (dmg system stop)')
        try:
            self.server_managers[0].system_stop()
        except ServerFailed as error:
            self.fail(error)

        start = float(time.time())
        self.log_step('Starting all engines (dmg system start)')
        try:
            self.server_managers[0].system_start()
        except ServerFailed as error:
            self.fail(error)

        duration = float(time.time()) - start
        self.log_step('Verifying all engines started in 120 seconds: {}'.format(duration))
        if duration > 120:
            self.fail("DAOS not ready to accept requests within 2 minutes after restart")

        # Verify all the pools exists after the restart
        self.log_step('Verifying all {} pools exist after engine restart'.format(quantity))
        self.get_dmg_command().timeout = 360
        pool_uuids = self.get_dmg_command().get_pool_list_uuids(no_query=True)
        detected_pools = [uuid.lower() for uuid in pool_uuids]
        missing_pools = []
        for pool in pools:
            pool_uuid = pool.uuid.lower()
            if pool_uuid not in detected_pools:
                missing_pools.append(pool_uuid)
        if missing_pools:
            self.fail(
                'The following created pools were not detected in the pool '
                'list after rebooting the servers:\n  [{}]: {}'.format(
                    len(missing_pools), ", ".join(missing_pools)))
        if len(pools) != len(detected_pools):
            self.fail('Incorrect number of pools detected after rebooting the servers')
        self.log_step('Test passed')
