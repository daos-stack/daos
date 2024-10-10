"""
(C) Copyright 2021-2024 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from test_utils_pool import add_pool, check_pool_creation


class PoolCreateCapacityTests(TestWithServers):
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
        daos_restart_duration = self.params.get("restart_duration", "/run/server_config/*", 0)
        pool_create_duration = self.params.get("create_duration", "/run/pool/*", 0)
        pool_quantity = self.params.get("quantity", "/run/pool/*", None)

        # Define all the pools with the same size defined in the test yaml
        self.log_step(f"Defining {pool_quantity[1]} pools to create")
        pools = []
        for _ in range(pool_quantity[1]):
            pools.append(add_pool(self, create=False))

        # Create all the pools
        self.log_step(f"Attempt to create {pool_quantity[1]} pools (dmg pool create)")
        self.get_dmg_command().server_set_logmasks("DEBUG", raise_exception=False)
        pools = check_pool_creation(self, pools, pool_create_duration, minimum=pool_quantity[0])
        self.get_dmg_command().server_set_logmasks(raise_exception=False)

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
            f"Verifying all engines started in {daos_restart_duration}s")
        if duration > daos_restart_duration:
            self.fail(
                'DAOS file system is not ready to accept requests within '
                f"{daos_restart_duration}s after restart: got={duration}s")

        # Verify all the pools exists after the restart
        self.log_step(f"Verifying all {len(pools)} pools exist after engines restart")
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
                f"{len(missing_pools)} pools are missing after engines restart: "
                f"miss=[{', '.join(missing_pools)}]")
        if len(pools) != len(detected_pools):
            self.fail(
                'Incorrect number of pools detected after engines restart: '
                f"wait={len(pools)}, got={len(detected_pools)}")

        self.log_step('Test passed')
