#!/usr/bin/python3
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import TestWithServers


class DestroyRebuild(TestWithServers):
    """Test class for pool destroy tests.

    Test Class Description:
        This test verifies destruction of a pool that is rebuilding.

    :avocado: recursive
    """

    # also remove the commented line form yaml file for rank 0
    CANCEL_FOR_TICKET = [["DAOS-4891", "rank_to_kill", "[0]"]]

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
        ranks = self.params.get("rank_to_kill", "/run/testparams/*")

        # Create a pool
        self.add_pool()

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
        self.get_dmg_command().system_start(",".join(ranks))
        self.server_managers[0].update_expected_states(",".join(ranks), ["joined"])
