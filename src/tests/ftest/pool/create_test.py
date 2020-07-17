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

from apricot import TestWithServers, skipForTicket
from bytes_utils import Bytes
from server_utils import ServerFailed


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

        super(PoolCreateTests, self).setUp()
        self.dmg = self.get_dmg_command()

    def get_max_pool_sizes(self, scm_ratio=0.9, nvme_ratio=0.9):
        """Get the maximum pool sizes for the current server configuration.

        Args:
            scm_ratio (float, optional): percentage of the maximum SCM
                capacity to use for the pool sizes. Defaults to 0.9 (90%).
            nvme_ratio (float, optional): percentage of the maximum NVMe
                capacity to use for the pool sizes. Defaults to 0.9 (90%).

        Returns:
            list: a list of Bytes objects representing the maximum pool creation
                SCM size and NVMe size

        """
        try:
            sizes = self.server_managers[0].get_available_storage()
        except ServerFailed as error:
            self.fail(error)

        ratios = (scm_ratio, nvme_ratio)
        for index, size in enumerate(sizes):
            if size and ratios[index] < 1:
                # Reduce the size by the specified percentage
                size.amount *= ratios[index]
                self.log.info(
                    "Adjusted %s size by %.2f%%: %s",
                    "SCM" if index == 0 else "NVMe", 100 * ratios[index],
                    str(sizes[index]))
        return sizes

    def define_pools(self, quantity, scm_ratio, nvme_ratio):
        """Define a list of TestPool objects.

        Args:
            quantity (int): number of TestPool objects to create
            scm_ratio (float): percentage of the maximum SCM capacity to use
                for the pool sizes, e.g. 0.9 for 90%
            nvme_ratio (float): percentage of the maximum NVMe capacity to use
                for the pool sizes, e.g. 0.9 for 90%. Specifying None will
                setup each pool without NVMe.
        """
        sizes = self.get_max_pool_sizes(
            scm_ratio, 1 if nvme_ratio is None else nvme_ratio)
        self.pool = [
            self.get_pool(create=False, connect=False) for _ in range(quantity)]
        for pool in self.pool:
            pool.scm_size.update(str(sizes[0]), "scm_size")
            if nvme_ratio is not None:
                if sizes[1] is None:
                    self.fail(
                        "Unable to assign a max pool NVMe size; NVMe not "
                        "configured!")

                # The I/O server allocates NVMe storage on targets in multiples
                # of 1GiB per target.  An server with 8 targets will have a
                # minimum NVMe size of 8 GiB.  Specify the largest NVMe size in
                # GiB that can be used with the configured number of targets and
                # specified capacity in GB.
                targets = self.server_managers[0].get_config_value("targets")
                nvme_multiple = Bytes(int(targets), "GiB")
                multiplier = 1
                while nvme_multiple <

                # The minimum NVMe size is 8 GiB.
                min_nvme_size = Bytes(8, "GiB")
                if sizes[1] < min_nvme_size:
                    self.log.warning(
                        "Calculated NVMe size %s is too small.  Using %s "
                        "minimum NVMe size.", str(sizes[1]), str(min_nvme_size))
                    pool.nvme_size.update(str(min_nvme_size), "nvme_size")
                else:
                    pool.nvme_size.update(str(sizes[1]), "nvme_size")

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

        :avocado: tags=all,pr,hw,large,pool,create_max_pool_scm_only
        """
        # Create 1 pool using 90% of the available SCM capacity (no NVMe)
        self.define_pools(1, 0.9, None)
        self.check_pool_creation(120)

    def test_create_max_pool(self):
        """JIRA ID: DAOS-3599.

        Test Description:
            Create a single pool that utilizes all the persistent memory and all
            the SSD capacity on all of the servers.  Verify that pool creation
            takes less than 4 minutes.

        :avocado: tags=all,pr,hw,large,pool,create_max_pool
        """
        # Create 1 pool using 90% of the available capacity
        self.define_pools(1, 0.9, 0.9)
        self.check_pool_creation(240)

    @skipForTicket("DAOS-5202")
    def test_create_pool_quantity(self):
        """JIRA ID: DAOS-3599.

        Test Description:
            Create 100 pools on all of the servers.
            Perform an orderly system shutdown via cmd line (dmg).
            Restart the system via cmd line tool (dmg).
            Verify that DAOS is ready to accept requests with in 2 minutes.

        :avocado: tags=all,pr,hw,large,pool,create_performance
        """
        # Create some number of pools each using a equal amount of 60% of the
        # available capacity, e.g. 0.6% for 100 pools.
        quantity = self.params.get("quantity", "/run/pool/*", 1)
        ratio = 0.6 / quantity
        self.define_pools(quantity, ratio, ratio)
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
        pool_uuid_list = [pool.uuid for pool in self.pool]
        result = self.dmg.get_output("pool_list")
        self.assertListEqual(
            pool_uuid_list, result,
            "Pool UUID list does not match after reboot")

    @skipForTicket("DAOS-5203")
    def test_create_no_space(self):
        """JIRA ID: DAOS-3728.

        Test Description:
            Create a pool using most of the capacity of one server on only one
            server.  Verify that attempting to create  a pool of the same size
            across all of the servers fails due to no space.  Now verify that
            creating a pool of the same size on across all but the first pool
            succeeds.

        :avocado: tags=all,pr,hw,large,pool,create_no_space
        """
        # Define three pools to create:
        #   - one pool using 90% of the available capacity of one server
        #   - one pool using 90% of the available capacity of all servers
        #   - one pool using 90% of the available capacity of the other server
        self.define_pools(3, 0.9, 0.9)
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

    @skipForTicket("DAOS-5203")
    def test_create_no_space_loop(self):
        """JIRA ID: DAOS-3728.

        Test Description:
            Create a pool using most of the capacity of one server on only one
            server.  Verify that attempting to create  a pool of the same size
            across all of the servers fails due to no space.  Now verify that
            creating a pool of the same size on across all but the first pool
            succeeds.  Reapeat the last two steps 100 times with the addition
            of deleting the successfully created pool to verify that there is
            not any subtle/low capacity space being lost with each failed
            create.

        :avocado: tags=all,pr,hw,large,pool,create_no_space_loop
        """
        # Define three pools to create:
        #   - one pool using 90% of the available capacity of one server
        #   - one pool using 90% of the available capacity of all servers
        #   - one pool using 90% of the available capacity of the other server
        self.define_pools(3, 0.9, 0.9)
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
