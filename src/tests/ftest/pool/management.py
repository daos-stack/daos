#!/usr/bin/python3
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers
from ior_utils import Ior


class TestPoolManagement(TestWithServers):
    """[summary]."""

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.daos = self.get_daos_command()
        self.pool = []
        self.container = []

    def verify_pool(self, pool_namespaces):
        """[summary]

        Args:
            pool_namespaces ([type]): [description]
        """
        for namespace in pool_namespaces:
            # Create a new pool
            self.pool.append(self.get_pool(namespace=namespace, connect=False))

            # Create a container for each new pool
            self.container.append(self.get_container(self.pool[-1]))

            # Run dmg/daos pool query
            self.pool[-1].query(self.daos)

            # Verify the pool sizes

    def test_pool_management(self):
        """JIRA DAOS-3672.

        Test Description:
            Verify pool space usage via cmd line tool.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm,small
        :avocado: tags=pool,management
        :avocado: tags=test_pool_management
        """
        self.job_manager.assign_hosts(
            self.hostlist_clients, self.workdir, self.hostfile_clients_slots)

        # Create and verify a single pool on a single server
        self.verify_pool(["/run/pool_0_0/*"])

        # Use IOR to fill containers to varying degrees of fullness, verify storage listing
        ior = Ior(
            self.job_manager, self.server_managers[0].group, self.pool[0], namespace="/run/ior_0/*")
        ior.get_params(self)
        ior.run(self.server_managers[0], self.client_log)

        # Create and verify multiple pools on a single server
        self.verify_pool(["/run/pool_1_{}/*".format(num) for num in range(3)])

        # Fill containers to varying degrees of fullness, verify storage listing for all pools

        # Create and verify a single pool that spans many servers
        self.verify_pool(["/run/pool_2_0/*"])

        # Use IOR to fill containers to varying degrees of fullness, verify storage listing

        # Create and verify multiple pools that span many servers
        self.verify_pool(["/run/pool_3_{}/*".format(num) for num in range(2)])

        # Use IOR to fill containers to varying degrees of fullness, verify storage listing

        # Fail one of the servers for a pool spanning many servers. Verify the storage listing.
