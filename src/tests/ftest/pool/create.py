"""
(C) Copyright 2021-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from test_utils_pool import add_pool, check_pool_creation, get_size_params


class PoolCreateTests(TestWithServers):
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

    def test_create_max_pool_scm_only(self):
        """JIRA ID: DAOS-5114 / SRS-1.

        Test Description:
            Create a single pool that utilizes all the persistent memory on all
            of the servers. Verify that the pool creation takes no longer than
            1 minute.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=PoolCreateTests,test_create_max_pool_scm_only
        """
        # Create 1 pool using 90% of the available SCM capacity (no NVMe)
        data = self.server_managers[0].get_available_storage()
        params = {"scm_size": int(float(data["scm"]) * 0.9)}
        pool = add_pool(self, namespace="/run/pool_1/*", create=False, **params)
        check_pool_creation(self, [pool], 60)

    def test_create_max_pool(self):
        """JIRA ID: DAOS-5114 / SRS-3.

        Test Description:
            Create a single pool that utilizes all the persistent memory and all
            the SSD capacity on all of the servers.  Verify that pool creation
            takes less than 2 minutes.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=PoolCreateTests,test_create_max_pool
        """
        # Create 1 pool using 90% of the available capacity
        pool = add_pool(self, namespace="/run/pool_2/*", create=False)
        check_pool_creation(self, [pool], 120)

    def test_create_no_space_loop(self):
        """JIRA ID: DAOS-3728.

        Test Description:
            Create a pool using most of the capacity of one server on only one
            server.  Verify that attempting to create  a pool of the same size
            across all of the servers fails due to no space.  Now verify that
            creating a pool of the same size on across all but the first server
            succeeds.  Repeat the last two steps 100 times with the addition of
            deleting the successfully created pool to verify that there is not
            any subtle/low capacity space being lost with each failed create.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=PoolCreateTests,test_create_no_space_loop
        """
        # Define three pools to create:
        #   - one pool using 90% of the available capacity of one server
        #   - one pool using 90% of the available capacity of all servers
        #   - one pool using 90% of the available capacity of the other server
        ranks = sorted(self.server_managers[0].ranks.keys())
        params = (
            {"target_list": ranks[:1]},
            {"target_list": ranks},
            {"target_list": ranks[1:]},
        )

        # Disable failing the test if a pool create fails
        self.get_dmg_command().exit_status_exception = False

        # Create the first of three pools which should succeed.
        pools = [add_pool(self, namespace="/run/pool_2/*", create=False, **params[0])]
        self.log.info("Creating")
        pools[0].create()
        self.assertTrue(
            pools[0].dmg.result.exit_status == 0,
            "Creating a large capacity pool on a single server should succeed."
        )

        # Setup the 2nd and 3rd pools using the same pool size as the first pool
        size_params = get_size_params(pools[0])
        for index in range(1, 3):
            params[index].update(size_params)
            pools.append(
                add_pool(self, namespace="/run/pool_2/*", create=False, **params[index]))

        for index in range(100):
            # Create the second of three pools which should fail due to not enough space.
            self.log.info("Loop %s", index)
            pools[1].create()
            result = pools[1].dmg.result
            if result.exit_status != 1 or "-1007" not in result.stdout_text:
                self.fail(
                    "Creating a large capacity pool across all servers should fail due to an "
                    "existing pool on one server consuming the required space.")

            # Create the third of three pools which should succeed.
            pools[2].create()
            if pools[2].dmg.result.exit_status != 0:
                self.fail(
                    "Creating a large capacity pool that spans across all but the first server "
                    "should succeed.")

            # Destroy the third of three pools so it can be created again in the next loop
            pools[2].destroy()
            if pools[2].dmg.result.exit_status != 0:
                self.fail(
                    "Destroying a large capacity pool that spans across all but the first server "
                    "should succeed.")
