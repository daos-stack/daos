#!/usr/bin/python
"""
(C) Copyright 2020 Intel Corporation.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
The Government's rights to use, modify, reproduce, release, perform, display,
or disclose this software are subject to the terms of the Apache License as
provided in Contract No. B609815.
Any reproduction of computer software, computer software documentation, or
portions thereof marked with this legend must also reproduce the markings.
"""
import time

from pool_test_base import PoolTestBase
from server_utils import ServerFailed
from apricot import skipForTicket


class PoolCreateTests(PoolTestBase):
    # pylint: disable=too-many-ancestors
    """Pool create tests.

    All of the tests verify pool create performance with 7 servers and 1 client.
    Each server should be configured with full compliment of NVDIMMs and SSDs.

    :avocado: recursive
    """

    def check_pool_creation(self, max_duration):
        """Check the duration of each pool creation meets the requirement.

        Args:
            max_duration (int): max pool creation duration allowed in seconds

        """
        durations = []
        for index, pool in enumerate(self.pool):
            start = float(time.time())
            pool.create()
            durations.append(float(time.time()) - start)
            self.log.info(
                "Pool %s creation: %s seconds", index + 1, durations[-1])

        exceeding_duration = 0
        for index, duration in enumerate(durations):
            if duration > max_duration:
                exceeding_duration += 1

        self.assertEqual(
            exceeding_duration, 0,
            "Pool creation took longer than {} seconds on {} pool(s)".format(
                max_duration, exceeding_duration))

    def test_create_max_pool_scm_only(self):
        """JIRA ID: DAOS-3599.

        Test Description:
            Create a single pool that utilizes all the persistent memory on all
            of the servers. Verify that the pool creation takes no longer than
            2 minutes.

        :avocado: tags=all,daily_regression,hw,large,pool
        :avocado: tags=create_max_pool_scm_only
        """
        # Create 1 pool using 90% of the available SCM capacity (no NVMe)
        self.pool = self.get_pool_list(1, 0.9, None, 1)
        self.check_pool_creation(120)

    def test_create_max_pool(self):
        """JIRA ID: DAOS-3599.

        Test Description:
            Create a single pool that utilizes all the persistent memory and all
            the SSD capacity on all of the servers.  Verify that pool creation
            takes less than 4 minutes.

        :avocado: tags=all,daily_regression,hw,large,pool,create_max_pool
        """
        # Create 1 pool using 90% of the available capacity
        self.pool = self.get_pool_list(1, 0.9, 0.9, 1)
        self.check_pool_creation(240)

    def test_create_pool_quantity(self):
        """JIRA ID: DAOS-3599.

        Test Description:
            Create 100 pools on all of the servers.
            Perform an orderly system shutdown via cmd line (dmg).
            Restart the system via cmd line tool (dmg).
            Verify that DAOS is ready to accept requests with in 2 minutes.

        :avocado: tags=all,pr,daily_regression,hw,large,pool,create_performance
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

    def test_create_no_space(self):
        """JIRA ID: DAOS-3728.

        Test Description:
            Create a pool using most of the capacity of one server on only one
            server.  Verify that attempting to create  a pool of the same size
            across all of the servers fails due to no space.  Now verify that
            creating a pool of the same size on across all but the first pool
            succeeds.

        :avocado: tags=all,pr,daily_regression,hw,large,pool,create_no_space
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

        :avocado: tags=all,pr,daily_regression,hw,large,pool
        :avocado: tags=create_no_space_loop
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
