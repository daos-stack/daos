"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase


# pylint: disable=too-few-public-methods
class RbldPoolDestroyWithIO(IorTestBase):
    """Rebuild test cases featuring IOR.

    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild using IOR.

    :avocado: recursive
    """

    def test_pool_destroy_with_io(self):
        """Jira ID: DAOS-3794.

        Test Description: Destroy pool when rebuild is ongoing.
                          I/O performed using IOR.

        Use Cases:
          Create pool
          Create 5 containers
          Perform io using ior with RP_3GX replication.
          Kill one of the ranks and trigger rebuild.
          Destroy Pool during rebuild.
          Re-create pool on remaining ranks.

        :avocado: tags=all,pr
        :avocado: tags=hw,medium,md_on_ssd
        :avocado: tags=pool,rebuild,ior
        :avocado: tags=RbldPoolDestroyWithIO,test_pool_destroy_with_io
        """
        # set params
        targets = self.server_managers[0].get_config_value("targets")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")
        engines_per_host = self.params.get("engines_per_host", "/run/server_config/*")

        # create pool
        self.create_pool()

        # make sure pool looks good before we start
        checks = {
            "pi_nnodes": len(self.hostlist_servers) * engines_per_host,
            "pi_ntargets": len(self.hostlist_servers) * targets * engines_per_host,
            "pi_ndisabled": 0,
        }
        self.assertTrue(
            self.pool.check_pool_info(**checks),
            "Invalid pool information detected before rebuild")

        self.assertTrue(
            self.pool.check_rebuild_status(rs_errno=0, rs_state=1, rs_obj_nr=0, rs_rec_nr=0),
            "Invalid pool rebuild info detected before rebuild")

        # perform first set of io using IOR
        for run in range(4):
            self.log.info("Starting ior run number %s", run)
            self.run_ior_with_pool()

        # Kill the server and trigger rebuild
        self.log.info("Starting rebuild by killing rank %s", rank)
        self.server_managers[0].stop_ranks([rank], self.d_log, force=True)

        # Wait for rebuild to start.
        self.log.info("Wait for rebuild to start")
        self.pool.wait_for_rebuild_to_start(interval=1)

        rebuild_state = self.pool.get_rebuild_state(True)
        self.log.info("Pool %s rebuild status:%s", self.pool.uuid, rebuild_state)

        self.log.info("Destroy pool %s while rebuild is %s", self.pool.uuid, rebuild_state)
        self.pool.destroy()
        self.container = None

        # re-create the pool of full size to verify the space was reclaimed,
        # after re-starting the server on excluded rank
        self.create_pool()
        self.pool.query()
