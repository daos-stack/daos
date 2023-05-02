'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import TestWithServers


class DestroyRebuild(TestWithServers):
    """Test class for pool destroy tests.

    Test Class Description:
        This test verifies destruction of a pool that is rebuilding.

    :avocado: recursive
    """

    def test_destroy_while_rebuilding(self):
        """Jira ID: DAOS-7100.

        Test Description:
            Create a pool across multiple servers. After excluding one of the
            servers, verify that the pool can be destroyed during rebuild.

        Use Cases:
            Verifying that a pool can be destroyed during rebuild.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool,rebuild
        :avocado: tags=DestroyRebuild,test_destroy_while_rebuilding
        """
        # Get the test parameters
        targets = self.server_managers[0].get_config_value("targets")
        ranks = self.params.get("rank_to_kill", "/run/testparams/*")

        # 1.
        self.log_step("Create a pool")
        pool = self.get_pool()

        # 2.
        self.log_step("Verify the pool information before starting rebuild")
        checks = {
            "pi_nnodes": len(self.hostlist_servers),
            "pi_ntargets": len(self.hostlist_servers) * targets,
            "pi_ndisabled": 0,
        }
        self.assertTrue(
            pool.check_pool_info(**checks),
            "Invalid pool information detected prior to rebuild")

        # 3.
        self.log_step("Start rebuild, system stop")
        self.server_managers[0].stop_ranks(ranks, self.d_log, force=True)
        pool.wait_for_rebuild_to_start()

        # 4.
        self.log_step("Destroy the pool while rebuild is active")
        pool.destroy()

        # 5.
        self.log_step("System start")
        self.get_dmg_command().system_start(list_to_str(value=ranks))

        # 6.
        self.log_step("Check restarted server in join state")
        self.server_managers[0].update_expected_states(ranks, ["joined"])
        self.log.info("Test Passed")
