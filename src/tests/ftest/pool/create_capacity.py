"""
(C) Copyright 2021-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import sys
import time

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from general_utils import get_display_size, human_to_bytes
from server_utils import ServerFailed
from test_utils_pool import add_pool


class PoolCreateCapacityTests(TestWithServers):
    """Pool create tests.

    All of the tests verify pool create performance with 7 servers and 1 client.
    Each server should be configured with full compliment of NVDIMMs and SSDs.

    :avocado: recursive
    """

    DER_NOSPACE = "DER_NOSPACE(-1007)"

    def __init__(self, *args, **kwargs):
        """Initialize a PoolCreateCapacityTests object."""
        super().__init__(*args, **kwargs)

        self.daos_restart_duration = 0
        self.pool_create_duration = 0
        self.pool_quantity = None
        self.pools = []

    def setUp(self):
        """Set up each test case."""
        # Create test-case-specific DAOS log files
        self.update_log_file_names()

        super().setUp()

        self.daos_restart_duration = self.params.get("restart_duration", "/run/server_config/*", 0)
        self.pool_create_duration = self.params.get("create_duration", "/run/pool/*", 0)
        self.pool_quantity = self.params.get("quantity", "/run/pool/*", None)

    def tearDown(self):
        """Tear down each test case."""
        self.destroy_pools(self.pools)

        super().tearDown()

    def create_pools(self):
        """Create a given number of pools."""
        self.log.debug("Defining pools to create")
        for _ in range(self.pool_quantity[1]):
            self.pools.append(add_pool(self, create=False))

        self.log.debug('Creating {} pools (dmg pool create)'.format(self.pool_quantity))
        try:
            for index, pool in enumerate(self.pools):
                start = time.time()
                pool.create()
                duration = time.time() - start

                if duration > self.pool_create_duration:
                    self.fail(
                        f"Creating pool {index} took longer than "
                        f"{self.pool_create_duration}s: got={duration}s")

        except TestFail as error:
            self.assertIn(
                self.DER_NOSPACE, str(error),
                f"Unexpected error occcured: wait={self.DER_NOSPACE}, got={str(error)}")

            self.destroy_pools(self.pools[index])
            self.pools = self.pools[:index]

            self.assertGreaterEqual(
                index, self.pool_quantity[0],
                'Quantity of pools created should be greater or equal to '
                f"{self.pool_quantity[0]}: got={index}")

            self.log.info(
                'Quantity of pools created lower than expected: '
                f"wait={self.pool_quantity[1]}, got={index}")


    def test_create_pool_quantity(self):
        """JIRA ID: DAOS-5114 / SRS-2 / SRS-4.

        Test Description:
            Create a given number pools on all of the servers.
            Perform an orderly system shutdown via cmd line (dmg).
            Restart the system via cmd line tool (dmg).
            Verify that DAOS is ready to accept requests within 2 minutes.
            Verify that all the created pools exists after the restart

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=PoolCreateCapacityTests,test_create_pool_quantity
        """
        # Define all the pools with the same size defined in the test yaml
        self.log_step('Creating {} pools'.format(self.pool_quantity[1]))
        self.create_pools()

        # Shutdown  DAOS file system')
        self.log_step('Stopping all engines (dmg system stop)')
        self.server_managers[0].system_stop()

        # Restarting DAOS file system
        self.log_step('Starting all engines (dmg system start)')
        start = float(time.time())
        self.server_managers[0].system_start()
        duration = float(time.time()) - start

        # Verify that DAOS is ready to accept requests within a duration defined in the test yaml
        self.log_step(
            f"Verifying all engines started in {self.daos_restart_duration}s")
        if duration > self.daos_restart_duration:
            self.fail(
                'DAOS file system is not ready to accept requests within '
                f"{self.daos_restart_duration}s after restart: got={duration}s")

        # Verify all the pools exists after the restart
        self.log_step(f"Verifying all {len(self.pools)} pools exist after engines restart")
        self.get_dmg_command().timeout = 360
        pool_uuids = self.get_dmg_command().get_pool_list_uuids(no_query=True)
        detected_pools = [uuid.lower() for uuid in pool_uuids]
        missing_pools = []
        for pool in self.pools:
            pool_uuid = pool.uuid.lower()
            if pool_uuid not in detected_pools:
                missing_pools.append(pool_uuid)
        if missing_pools:
            self.fail(
                f"{len(missing_pools)} pools are missing after engines restart: "
                f"miss=[{', '.join(missing_pools)}]")
        if len(self.pools) != len(detected_pools):
            self.fail(
                'Incorrect number of pools detected after engines restart: '
                f"wait={len(self.pools)}, got={len(detected_pools)}")

        self.log_step('Test passed')
