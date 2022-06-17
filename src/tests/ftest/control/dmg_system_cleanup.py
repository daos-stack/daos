#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail
from pydaos.raw import DaosPool
from apricot import TestWithServers
from socket import gethostname

class DmgSystemCleanupTest(TestWithServers):
    """Test Class Description:
    This test covers the following requirement.
    (SRS-326) Pool Management - automatic pool handle revocation

    Test step:
    1. Create 2 pools.
    2. Create a container in each pool.
    3. Write to each container to ensure connections are working.
    4. Call dmg system cleanup host.
    5. Write to each container again to ensure they fail
    6. Check that cleaned up handles match expected counts.

    :avocado: recursive
    """

    def test_dmg_system_cleanup_one_host(self):
        """
        JIRA ID: DAOS-6471

        Test Description: Test dmg system cleanup.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=control,dmg
        :avocado: tags=dmg_system_cleanup,test_dmg_system_cleanup_one_host
        """
        # Print out where this is running
        hostname = gethostname().split(".")[0]
        self.log.info("Script is running on %s", hostname)

        # Create 2 pools and create a container in each pool.
        self.pool = []
        self.container = []
        for _ in range(2):
            self.pool.append(self.get_pool())
            self.container.append(self.get_container(self.pool[-1]))

        # Create 5 more connections to each pool
        pool_handles = []
        for pool in self.pool:
            for _ in range(5):
                handle = self.get_pool(create=False, connect=False)
                handle.pool = DaosPool(self.context)
                handle.uuid = pool.uuid
                handle.connect(2)
                pool_handles.append(handle)

        # Check to make sure we can access the pool
        try:
            for i in range(2):
                self.container[i].write_objects()
        except TestFail as error:
            self.fail("Unable to write container #{}: {}\n".format(i, error))

        # Call dmg system cleanup on the host and create cleaned pool list.
        dmg_cmd = self.get_dmg_command()
        result = dmg_cmd.system_cleanup(self.agent_managers[0].hosts, verbose=True)

        # Build list of pools and how many handles were cleaned (should be 6 each)
        actual_counts = dict()
        for res in result["response"]["results"]:
            if res["status"] == 0:
                actual_counts[res["pool_id"].lower()] = res["count"]
        # Attempt to access the pool again (should fail)
        for i in range(2):
            try:
                self.container[i].write_objects()
                self.fail("Wrote to container #{} when it should have failed:\n".format(i))
            except TestFail as error:
                self.log.info("Unable to write container #%d: as expected %s\n", i, error)

        # Build a list of pool IDs and counts (6) to compare against
        # our cleanup results.
        expected_count = {pool.uuid.lower(): 6 for pool in self.pool}

        # Clear pool and container list to avoid trying to destroy them.
        self.pool = []
        self.container = []

        # Compare results
        self.assertDictEqual(expected_count, actual_counts,
                             "Cleaned up handles do not match the expected amount.")

        self.log.info("Test passed!")
