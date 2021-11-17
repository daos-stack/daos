#!/usr/bin/python3
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import TestWithServers
from general_utils import convert_list


class DestroyRebuild(TestWithServers):
    """Test class for pool destroy tests.

    Test Class Description:
        This test verifies destruction of a pool that is rebuilding.

    :avocado: recursive
    """

    def test_destroy_while_rebuilding(self):
        """Jira ID: DAOS-xxxx.

        Test Description:
            Create a pool across multiple servers. After excluding one of the
            servers, verify that the pool can be destroyed during rebuild.

        Use Cases:
            Verifying that a pool can be destroyed during rebuild.

        :avocado: tags=all,daily_regression
        :avocado: tags=medium
        :avocado: tags=pool,destroy_pool_rebuild
        """
        # Get the test parameters
        targets = self.params.get("targets", "/run/server_config/servers/*")

        # Create a pool
        self.add_pool()

        # kill 2 ranks, including 1 svc rank
        ranks = self.pool.choose_rebuild_ranks(num_ranks=2, num_svc=1)

        # Verify the pool information before starting rebuild
        checks = {
            "pi_nnodes": len(self.hostlist_servers),
            "pi_ntargets": len(self.hostlist_servers) * targets,
            "pi_ndisabled": 0,
        }
        self.assertTrue(
            self.pool.check_pool_info(**checks),
            "Invalid pool information detected prior to rebuild")

        # Start rebuild
        self.server_managers[0].stop_ranks(ranks, self.d_log, force=True)
        self.pool.wait_for_rebuild(True)

        # Destroy the pool while rebuild is active
        self.pool.destroy()

        self.log.info("Test Passed")
        self.get_dmg_command().system_start(convert_list(ranks))
        self.server_managers[0].update_expected_states(ranks, ["joined"])
