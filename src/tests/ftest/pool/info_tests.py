#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class InfoTests(TestWithServers):
    """Test class for pool query.

    Test Class Description:
        This test verifies destruction of a pool that is rebuilding.

    :avocado: recursive
    """

    def test_pool_info_query(self):
        """Jira ID: DAOS-xxxx.

        Test Description:
            Create and connect to a pool.  Verify the pool information returned
            from the pool query matches the values used to create the pool.

        Use Cases:
            Verify pool query.

        :avocado: tags=all,daily_regression,
        :avocado: tags=tiny
        :avocado: tags=pool,smoke,info_test
        """
        # Get the test params
        permissions = self.params.get("permissions", "/run/test/*")
        targets = self.params.get("targets", "/run/server_config/*")
        #pool_targets = len(self.hostlist_servers) * targets

        # Create a pool
        self.add_pool()

        # Connect to the pool
        self.pool.connect(1 << permissions)

        # Verify the pool information
        checks = {
            "pi_uuid": self.pool.uuid,
            "pi_ntargets": len(self.hostlist_servers) * targets,
            "pi_nnodes": len(self.hostlist_servers),
            "pi_ndisabled": 0,
            "pi_map_ver": 1,
            "pi_leader": 0,
            "pi_bits": 0xFFFFFFFFFFFFFFFF,
        }
        status = self.pool.check_pool_info(**checks)
        self.assertTrue(status, "Invalid pool information detected prior")
        checks = {
            "s_total": (self.pool.scm_size.value, 0),
            #"s_free": (self.pool.scm_size.value - (256 * pool_targets), 0),
        }
        status = self.pool.check_pool_daos_space(**checks)
        self.assertTrue(status, "Invalid pool space information detected")
        self.log.info("Test Passed")
