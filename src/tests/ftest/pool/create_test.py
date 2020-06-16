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
import os
import re
import time

from apricot import TestWithServers
from bytes_utils import Bytes


class PoolCreateTests(TestWithServers):
    """Pool create tests.

    All of the tests verify pool create performance with 7 servers and 1 client.
    Each server should be configured with full compliment of NVDIMMs and SSDs.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        super(PoolCreateTests, self).setUp()
        self.dmg = self.get_dmg_command()

    def get_system_state(self):
        """Get the current DAOS system state.

        Returns:
            str: the current DAOS system state

        """
        result = self.dmg.get_output("system_query")
        if not result:
            # The regex failed to get the rank and state
            self.fail("Error obtaining {} output: {}".format(self.dmg, result))
        if len(result) > 1:
            # Multiple states for different ranks detected
            self.fail(
                "Multiple system states detected:\n  {}".format(
                    "\n  ".join(result)))
        if len(result[0]) != 2:
            # Single state but missing ranks and state - should not occur.
            self.fail("Unexpected result from {}: {}".format(self.dmg, result))
        return result[0][1]

    def check_system_state(self, valid_states, max_checks=1):
        """Check that the DAOS system state is one of the provided states.

        Fail the test if the current state does not match one of the specified
        valid states.  Optionally the state check can loop multiple times,
        sleeping one second between checks, by increasing the number of maximum
        checks.

        Args:
            valid_states (list): expected DAOS system states as a list of
                lowercase strings
            max_checks (int, optional): number of times to check the state.
                Defaults to 1.

        Returns:
            str: current detected state

        """
        checks = 0
        daos_state = "????"
        while daos_state not in valid_states and checks < max_checks:
            daos_state = self.get_system_state().lower()
            checks += 1
            time.sleep(1)
        if daos_state not in valid_states:
            self.fail(
                "Error checking DAOS state, currently neither {} after "
                "{} state check(s)!".format(valid_states, checks))
        return daos_state

    def system_start(self):
        """Start the DAOS IO servers."""
        self.log.info("Starting DAOS IO servers")
        self.check_system_state(("stopped"))
        result = self.dmg.system_start()
        if result.exit_status != 0:
            self.fail("Error starting DAOS:\n{}".format(result))

    def system_stop(self):
        """Stop the DAOS IO servers."""
        self.log.info("Stopping DAOS IO servers")
        self.check_system_state(("started", "joined"))
        result = self.dmg.system_stop()
        if result.exit_status != 0:
            self.fail("Error stopping DAOS:\n{}".format(result))

    def get_available_storage(self):
        """Get the available SCM and NVMe storage.

        Returns:
            list: a list of Bytes objects representing the maximum available
                SCM size and NVMe size

        """
        using_dcpm = self.server_managers[0].manager.job.using_dcpm
        using_nvme = self.server_managers[0].manager.job.using_nvme

        if using_dcpm or using_nvme:
            # Stop the DAOS IO servers in order to be able to scan the storage
            self.system_stop()

            # Scan all of the hosts for their SCM and NVMe storage
            saved_hostlist = self.dmg.hostlist
            self.dmg.hostlist = self.hostlist_servers
            result = self.dmg.storage_scan(verbose=True)
            self.dmg.hostlist = saved_hostlist
            if result.exit_status != 0:
                self.fail("Error obtaining DAOS storage:\n{}".format(result))

            # Find the sizes of the SCM and NVMe storage configured for use by
            # the DAOS IO servers.  Convert the lists into hashable tuples so
            # they can be used as dictionary keys.
            scm_keys = [
                os.path.basename(path)
                for path in self.server_managers[0].get_config_value("scm_list")
                if path
            ]
            nvme_keys = self.server_managers[0].get_config_value("bdev_list")
            device_capacities = {}
            for key in scm_keys + nvme_keys:
                device_capacities[key] = None
            for key in device_capacities:
                regex = r"(?:{})\s+.*\d+\s+([0-9\.]+)\s+([A-Z])B".format(key)
                data = re.findall(regex, result.stdout)
                self.log.info("Storage detected for %s in %s:", key, data)
                for size in data:
                    capacity = Bytes(size[0], size[1])
                    self.log.info("  %s", capacity)
                    if device_capacities[key] is None:
                        # Store this value if no other value exists
                        device_capacities[key] = capacity
                    elif capacity < device_capacities[key]:
                        # Replace an existing capacity with a lesser value
                        device_capacities[key] = capacity

            # Sum the minimum available capacities for each SCM and NVMe device
            storage = []
            for keys in (scm_keys, nvme_keys):
                storage.append(sum([device_capacities[key] for key in keys]))
                storage[-1].convert_up()

            # Restart the DAOS IO servers
            self.system_start()

        else:
            # Report only the scm_size
            scm_size = self.server_managers[0].get_config_value("scm_size")
            storage = [Bytes(scm_size, "G"), None]

        self.log.info(
            "Total available storage:  \nSCM:  %s\n  NVMe: %s", str(storage[0]),
            str(storage[1]))
        return storage

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
        sizes = self.get_available_storage()
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
        if quantity > 1:
            # Divide the pool size up into the specified quantity
            for size in sizes:
                if size:
                    size.amount /= quantity
                    if "." in str(size):
                        size.convert_down()
        for pool in self.pool:
            pool.scm_size.update(str(sizes[0]), "scm_size")
            if nvme_ratio is not None:
                if sizes[1] is None:
                    self.fail(
                        "Unable to assign a max pool NVMe size; NVMe not "
                        "configured!")
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
        self.define_pools(1, 0.9, 0.9)
        self.check_pool_creation(240)

    def test_create_pool_quantity(self):
        """JIRA ID: DAOS-3599.

        Test Description:
            Create 100 pools on all of the servers.
            Perform an orderly system shutdown via cmd line (dmg).
            Restart the system via cmd line tool (dmg).
            Verify that DAOS is ready to accept requests with in 2 minutes.

        :avocado: tags=all,pr,hw,large,pool,create_performance
        """
        self.define_pools(100, 0.75, 0.75)
        self.check_pool_creation(3)
        self.system_stop()

        start = float(time.time())
        self.system_start()
        duration = float(time.time()) - start
        self.assertLessEqual(
            duration, 120,
            "DAOS not ready to accept requests with in 2 minutes")

        pool_uuid_list = [pool.uuid for pool in self.pool]
        result = self.dmg.get_output("pool_list")
        self.assertListEqual(
            pool_uuid_list, result,
            "Pool UUID list does not match after reboot")

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
        #   - one using all of the available capacity of one server
        #   - one using all of the available capacity of all servers
        #   - one using all of the available capacity of the other server
        self.define_pools(3, 0.9, 0.9)
        ranks = [rank for rank, _ in enumerate(self.hostlist_servers)]
        self.pool[0].target_list.value = ranks[0]
        self.pool[1].target_list.value = ranks
        self.pool[2].target_list.value = ranks[1:]

        # Disable failing the test if a pool create fails
        self.dmg.exit_status_exception = False

        # Create all three pools.  The creation of the first and third pools
        # should succeed.  The second pool creation should fail due to not
        # enough space.
        self.pool[0].create()
        self.assertTrue(
            self.pool[0].dmg_result.exit_status == 0,
            "Creating a large capacity pool on a single server should succeed."
        )
        self.pool[1].create()
        self.assertTrue(
            self.pool[1].dmg_result.exit_status == 1 and
            "-1007" in self.pool[1].dmg_result.stdout,
            "Creating a large capacity pool across all servers should fail "
            "due to an existing pool on one server consuming the required "
            "space."
        )
        self.pool[2].create()
        self.assertTrue(
            self.pool[2].dmg_result.exit_status == 0,
            "Creating a large capacity pool across all but the first server "
            "should succeed."
        )

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
        #   - one using all of the available capacity of one server
        #   - one using all of the available capacity of all servers
        #   - one using all of the available capacity of the other server
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
            self.pool[0].dmg_result.exit_status == 0,
            "Creating a large capacity pool on a single server should succeed."
        )
        for index in range(100):
            self.log.info("Loop %s", index)
            self.pool[1].create()
            self.assertTrue(
                self.pool[1].dmg_result.exit_status == 1 and
                "-1007" in self.pool[1].dmg_result.stdout,
                "Creating a large capacity pool across all servers should fail "
                "due to an existing pool on one server consuming the required "
                "space."
            )
            self.pool[2].create()
            self.assertTrue(
                self.pool[2].dmg_result.exit_status == 0,
                "Creating a large capacity pool that spans across all but the "
                "first server should succeed."
            )
            self.pool[2].destroy()
            self.assertTrue(
                self.pool[2].dmg_result.exit_status == 0,
                "Destroying a large capacity pool that spans across all but "
                "the first server should succeed."
            )
