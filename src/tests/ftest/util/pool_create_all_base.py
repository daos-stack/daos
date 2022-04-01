#!/usr/bin/python3
"""
(C) Copyright 2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class PoolCreateAllTestBase(TestWithServers):
    """Base class for testing pool creation with percentage storage.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a PoolCreateAllBaseTests object."""
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

    def create_one_pool(self):
        """Create one pool with all the available storage capacity"""
        self.add_pool_qty(1, namespace="/run/pool/*", create=False)
        self.pool[0].size.update("100%")

        self.log.info("Creating a pool with 100% of the available storage")
        self.pool[0].create()
        self.assertEqual(self.pool[0].dmg.result.exit_status, 0, "Pool could not be created")

    def destroy_one_pool(self, index):
        """Destroying one pool"""
        self.log.info("Destroying pool %d", index)
        self.pool[0].destroy()
        self.assertEqual(self.pool[0].dmg.result.exit_status, 0,
                "Pool {} could not be destroyed".format(index))

    def create_first_of_two_pools(self):
        """Create first pool with 50% the available storage capacity"""
        self.add_pool_qty(2, namespace="/run/pool/*", create=False)
        self.pool[0].size.update("50%")
        self.pool[1].size.update("100%")

        self.log.info("Creating a first pool with 50% of the available storage")
        self.pool[0].create()
        self.pool[0].get_info()
        self.assertEqual(self.pool[0].dmg.result.exit_status, 0,
                "First pool could not be created")

    def create_second_of_two_pools(self):
        """Create the second pool with 50% the available storage capacity"""
        self.log.info("Creating a second pool with 100% of the available storage")
        self.pool[1].create()
        self.pool[1].get_info()
        self.assertEqual(self.pool[1].dmg.result.exit_status, 0,
                "Second pool could not be created")
