#!/usr/bin/python3
"""
(C) Copyright 2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import sys

from apricot import TestWithServers
from command_utils_base import CommandFailure


class PoolCreateAllVmTests(TestWithServers):
    """Tests pool creation with percentage storage on functional platform.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a PoolCreateAllVmTest object."""
        super().__init__(*args, **kwargs)

        self.epsilon_bytes = 1 << 20 # 1MiB
        # Maximal size of DAOS metadata stored for one pool on a SCM device:
        #   - 1 GiB for the control plane RDB
        #   - 16 MiB for the other metadata
        # More details could be found with the definition of the constant mdDaosScmBytes in the file
        # src/control/server/ctl_storage_rpc.go
        self.max_scm_metadata_bytes = 1 << 30 + 1 << 24

    def setUp(self):
        """Set up each test case."""
        super().setUp()

        self.ranks_size = len(self.hostlist_servers)
        self.delta_bytes = self.ranks_size * self.epsilon_bytes

        self.scm_avail_bytes = self.get_pool_available_bytes()

    def get_pool_available_bytes(self):
        """Return the available size of SCM storage."""
        self.assertGreater(len(self.server_managers), 0, "No server managers")
        try:
            self.log.info("Retrieving available size")
            result = self.server_managers[0].dmg.storage_query_usage()
        except CommandFailure as error:
            self.fail("dmg command failed: {}".format(error))

        scm_vos_bytes = sys.maxsize
        scm_vos_size = 0
        for host_storage in result["response"]["HostStorage"].values():
            host_size = len(host_storage["hosts"].split(','))
            for scm_devices in host_storage["storage"]["scm_namespaces"]:
                scm_vos_bytes = min(scm_vos_bytes, scm_devices["mount"]["avail_bytes"])
                scm_vos_size += host_size

        self.log.info("Available VOS size: scm_bytes=%d, scm_size=%d",
                scm_vos_bytes, scm_vos_size)

        scm_pool_bytes = scm_vos_bytes * scm_vos_size if scm_vos_bytes != sys.maxsize else 0
        self.log.info("Available POOL size: scm_bytes=%d", scm_pool_bytes)

        return scm_pool_bytes

    def test_one_pool(self):
        """Test the creation of one pool with all the storage capacity

        Test Description:
            Create a pool with all the capacity of all servers. Verify that the pool created
            effectively used all the available storage and there is no more available storage.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_create_all
        :avocado: tags=pool_create_all_one_vm
        """
        self.log.info("Test  basic pool creation with full storage")

        self.log.info("Creating a pool with 100% of the available storage")
        self.add_pool_qty(1, namespace="/run/pool/*", create=False)
        self.pool[0].size.update("100%")
        self.pool[0].create()
        self.assertEqual(self.pool[0].dmg.result.exit_status, 0,
                "Pool could not be created")

        self.log.info("Checking size of the pool")
        self.pool[0].get_info()
        tier_bytes = self.pool[0].info.pi_space.ps_space.s_total
        self.assertLessEqual(abs(self.scm_avail_bytes - tier_bytes[0]), self.delta_bytes,
                "Invalid SCM size: want={}, got={}, delta={}".format(self.scm_avail_bytes,
                    tier_bytes[0], self.delta_bytes))
        self.assertEqual(0, tier_bytes[1],
                "Invalid SMD size: want=0, got={tier_bytes[1]}")

        self.log.info("Checking size of available storage")
        self.scm_avail_bytes = self.get_pool_available_bytes()
        self.assertEqual(0, self.scm_avail_bytes,
                "Invalid SCM size: want=0, got={}".format(self.scm_avail_bytes))

    def test_recycle_pools(self):
        """Test the pool creation and destruction

        Test Description:
            Create a pool with all the capacity of all servers. Verify that the pool created
            effectively used all the available storage. Destroy the pool and repeat these steps 100
            times. For each iteration, check that the size of the created pool is always the same.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_create_all
        :avocado: tags=pool_create_all_recycle_vm
        """
        self.log.info("Test pool creation and destruction")

        for index in range(10):
            self.log.info("Creating pool %d with 100%% of the available storage", index)
            self.add_pool_qty(1, namespace="/run/pool/*", create=False)
            self.pool[0].size.update("100%")
            self.pool[0].create()
            self.assertEqual(self.pool[0].dmg.result.exit_status, 0,
                    "Pool {} could not be created".format(index))

            self.log.info("Checking size of pool %d", index)
            self.pool[0].get_info()
            tier_bytes = self.pool[0].info.pi_space.ps_space.s_total
            self.assertLessEqual(abs(self.scm_avail_bytes - tier_bytes[0]), self.delta_bytes,
                    "Invalid SCM size: want={}, got={}, delta={}".format(self.scm_avail_bytes,
                        tier_bytes[0], self.delta_bytes))
            self.assertEqual(0, tier_bytes[1],
                    "Invalid SMD size: want=0, got={}".format(tier_bytes[1]))

            self.log.info("Destroying pool %d", index)
            self.pool[0].destroy()
            self.assertEqual(self.pool[0].dmg.result.exit_status, 0,
                    "Pool {} could not be destroyed".format(index))

            self.log.info("Checking size of available storage at iteration %d", index)
            scm_avail_bytes = self.get_pool_available_bytes()
            self.assertLessEqual(abs(scm_avail_bytes - tier_bytes[0]), self.delta_bytes,
                    "Invalid SCM size: want={}, got={}, delta={}".format(self.scm_avail_bytes,
                        tier_bytes[0], self.delta_bytes))
            self.assertEqual(0, tier_bytes[1],
                    "Invalid SMD size: want=0, got={}".format(tier_bytes[1]))

    def check_pool_distribution(self):
        """Check if the size used on each hosts is more or less uniform
        """
        self.assertGreater(len(self.server_managers), 0, "No server managers")
        try:
            self.log.info("Retrieving available size")
            result = self.server_managers[0].dmg.storage_query_usage()
        except CommandFailure as error:
            self.fail("dmg command failed: {}".format(error))

        scm_vos_bytes = -1
        scm_delta_bytes = -1
        for host_storage in result["response"]["HostStorage"].values():
            for scm_devices in host_storage["storage"]["scm_namespaces"]:
                scm_bytes = scm_devices["mount"]["total_bytes"]
                scm_bytes -= scm_devices["mount"]["avail_bytes"]

                # Update the average size used by one rank
                if scm_vos_bytes == -1:
                    scm_vos_bytes = scm_bytes
                    continue

                # Check the difference of size without the metadata
                delta_bytes = abs(scm_vos_bytes - scm_bytes)
                if delta_bytes < self.epsilon_bytes:
                    continue

                # Update the difference of size allowed with metadata
                if scm_delta_bytes == -1:
                    self.assertLessEqual(delta_bytes, self.max_scm_metadata_bytes,
                            "Invalid size of SCM meta data: max={}, "
                            "got={}".format(self.max_scm_metadata_bytes, delta_bytes))
                    scm_delta_bytes = delta_bytes
                    continue

                # Check the difference of size with metadata
                self.assertLessEqual(delta_bytes, scm_delta_bytes + self.epsilon_bytes,
                        "Invalid size of SCM used: want={} got={}".format(scm_vos_bytes, scm_bytes))

    def test_two_pools(self):
        """Test the creation of two pools with 50% and 100% of the available storage

        Test Description:
            Create a first pool with 50% of all the capacity of all servers. Verify that the pool
            created effectively used 50% of the available storage and also check that the pool is
            well balanced (i.e. more or less use the same size on all the available servers).
            Create a second pool with all the remaining storage. Verify that the pool created
            effectively used all the available storage and there is no more available storage.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,pool_create_all
        :avocado: tags=pool_create_all_two_vm
        """
        self.log.info("Test pool creation of two pools with 50% and 100% of the available storage")

        self.add_pool_qty(2, namespace="/run/pool/*", create=False)
        self.pool[0].size.update("50%")
        self.pool[1].size.update("100%")

        self.log.info("Creating a first pool with 50% of the available storage")
        self.pool[0].create()
        self.pool[0].get_info()
        self.assertEqual(self.pool[0].dmg.result.exit_status, 0,
                "First pool 0 could not be created")

        self.log.info("Checking size of the first pool")
        self.pool[0].get_info()
        tier_bytes = [self.pool[0].info.pi_space.ps_space.s_total]
        self.assertLessEqual(abs(self.scm_avail_bytes - 2 * tier_bytes[0][0]), self.delta_bytes,
                "Invalid SCM size: want={}, got={}, delta={}".format(self.scm_avail_bytes / 2,
                    tier_bytes[0][0], self.delta_bytes))
        self.assertEqual(0, tier_bytes[0][1], "Invalid SMD size: want=0, "
                "got={}".format(tier_bytes[0][1]))

        self.log.info("Checking the distribution of the first pool")
        self.check_pool_distribution()

        self.scm_avail_bytes = self.get_pool_available_bytes()

        self.log.info("Creating a second pool with 100% of the available storage")
        self.pool[1].create()
        self.pool[1].get_info()
        self.assertEqual(self.pool[1].dmg.result.exit_status, 0,
                "Second pool could not be created")

        self.pool[1].get_info()
        tier_bytes.append(self.pool[1].info.pi_space.ps_space.s_total)

        self.log.info("Checking size of the second pool with the old available size")
        self.assertLessEqual(abs(self.scm_avail_bytes - tier_bytes[1][0]), self.delta_bytes,
                "Invalid SCM size: want={}, got={}, delta={}".format(self.scm_avail_bytes,
                    tier_bytes[1][0], self.delta_bytes))
        self.assertEqual(0, tier_bytes[1][1], "Invalid SMD size: want=0, "
                "got={}".format(tier_bytes[1][1]))

        self.log.info("Checking size of the second pool with the size of the first pool")
        scm_delta_bytes = self.ranks_size * self.max_scm_metadata_bytes + self.delta_bytes
        self.assertLessEqual(abs(tier_bytes[0][0] - tier_bytes[1][0]), scm_delta_bytes,
                "Invalid SCM size: want={}, got={}, delta={}".format(tier_bytes[0][0],
                    tier_bytes[1][0], self.delta_bytes))
        self.assertEqual(0, tier_bytes[1][1], "Invalid SMD size: want=0, "
                "got={}".format(tier_bytes[1][1]))

        self.scm_avail_bytes = self.get_pool_available_bytes()

        self.log.info("Checking size of available storage after the creation of the second pool")
        self.assertLessEqual(self.scm_avail_bytes, self.delta_bytes,
                "Invalid SCM size: want=0, got={}".format(self.scm_avail_bytes))
