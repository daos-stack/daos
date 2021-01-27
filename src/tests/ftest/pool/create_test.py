#!/usr/bin/python
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from pool_test_base import PoolTestBase


class PoolCreateTests(PoolTestBase):
    # pylint: disable=too-many-ancestors
    """Pool create tests.

    All of the tests verify pool create performance with 7 servers and 1 client.
    Each server should be configured with full compliment of NVDIMMs and SSDs.

    :avocado: recursive
    """

    def test_create_max_pool_scm_only(self):
        """JIRA ID: DAOS-5114 / SRS-1.

        Test Description:
            Create a single pool that utilizes all the persistent memory on all
            of the servers. Verify that the pool creation takes no longer than
            1 minute.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=pool
        :avocado: tags=pool_create_tests,create_max_pool_scm_only
        """
        # Create 1 pool using 90% of the available SCM capacity (no NVMe)
        self.pool = self.get_pool_list(1, 0.9, None, 1)
        self.check_pool_creation(60)

    def test_create_max_pool(self):
        """JIRA ID: DAOS-5114 / SRS-3.

        Test Description:
            Create a single pool that utilizes all the persistent memory and all
            the SSD capacity on all of the servers.  Verify that pool creation
            takes less than 2 minutes.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=pool
        :avocado: tags=pool_create_tests,create_max_pool
        """
        # Create 1 pool using 90% of the available capacity
        self.pool = self.get_pool_list(1, 0.9, 0.9, 1)
        self.check_pool_creation(120)

    def test_create_no_space(self):
        """JIRA ID: DAOS-3728.

        Test Description:
            Create a pool using most of the capacity of one server on only one
            server.  Verify that attempting to create  a pool of the same size
            across all of the servers fails due to no space.  Now verify that
            creating a pool of the same size on across all but the first pool
            succeeds.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=pool
        :avocado: tags=pool_create_tests,create_no_space
        """
        # Define three pools to create:
        #   - one pool using 90% of the available capacity of one server
        #   - one pool using 90% of the available capacity of all servers
        #   - one pool using 90% of the available capacity of the other server
        self.pool = self.get_pool_list(3, 0.9, 0.9, 1)
        ranks = [rank for rank, _ in enumerate(self.hostlist_servers)]
        self.pool[0].target_list.update(ranks[:1], "pool[0].target_list")
        self.pool[1].target_list.update(ranks, "pool[1].target_list")
        self.pool[2].target_list.update(ranks[1:], "pool[2].target_list")

        # Disable failing the test if a pool create fails
        self.dmg.exit_status_exception = False

        # Create all three pools.  The creation of the first and third pools
        # should succeed.  The second pool creation should fail due to not
        # enough space.
        self.pool[0].create()
        self.assertEqual(
            self.pool[0].dmg.result.exit_status, 0,
            "Creating a large capacity pool on a single server should succeed."
        )
        self.pool[1].create()
        self.assertTrue(
            self.pool[1].dmg.result.exit_status == 1 and
            "-1007" in self.pool[1].dmg.result.stdout,
            "Creating a large capacity pool across all servers should fail "
            "due to an existing pool on one server consuming the required "
            "space."
        )
        self.pool[2].create()
        self.assertEqual(
            self.pool[2].dmg.result.exit_status, 0,
            "Creating a large capacity pool across all but the first server "
            "should succeed."
        )

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
        :avocado: tags=hw,large
        :avocado: tags=pool
        :avocado: tags=pool_create_tests,create_no_space_loop
        """
        # Define three pools to create:
        #   - one pool using 90% of the available capacity of one server
        #   - one pool using 90% of the available capacity of all servers
        #   - one pool using 90% of the available capacity of the other server
        self.pool = self.get_pool_list(3, 0.9, 0.9, 1)
        ranks = [rank for rank, _ in enumerate(self.hostlist_servers)]
        self.pool[0].target_list.update(ranks[:1], "pool[0].target_list")
        self.pool[1].target_list.update(ranks, "pool[1].target_list")
        self.pool[2].target_list.update(ranks[1:], "pool[2].target_list")

        # Disable failing the test if a pool create fails
        self.dmg.exit_status_exception = False

        # Create all three pools.  The creation of the first and third pools
        # should succeed.  The second pool creation should fail due to not
        # enough space.
        self.log.info("Creating")
        self.pool[0].create()
        self.assertTrue(
            self.pool[0].dmg.result.exit_status == 0,
            "Creating a large capacity pool on a single server should succeed."
        )
        for index in range(100):
            self.log.info("Loop %s", index)
            self.pool[1].create()
            self.assertTrue(
                self.pool[1].dmg.result.exit_status == 1 and
                "-1007" in self.pool[1].dmg.result.stdout,
                "Creating a large capacity pool across all servers should fail "
                "due to an existing pool on one server consuming the required "
                "space."
            )
            self.pool[2].create()
            self.assertTrue(
                self.pool[2].dmg.result.exit_status == 0,
                "Creating a large capacity pool that spans across all but the "
                "first server should succeed."
            )
            self.pool[2].destroy()
            self.assertTrue(
                self.pool[2].dmg.result.exit_status == 0,
                "Destroying a large capacity pool that spans across all but "
                "the first server should succeed."
            )
