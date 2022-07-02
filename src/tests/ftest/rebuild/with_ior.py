#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from ior_test_base import IorTestBase

# pylint: disable=too-few-public-methods,too-many-ancestors
class RbldWithIOR(IorTestBase):
    """Rebuild test cases featuring IOR.

    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild using IOR.

    :avocado: recursive
    """

    def test_rebuild_with_ior(self):
        """Jira ID: DAOS-951.

        Test Description: Trigger a rebuild while I/O is ongoing.
                          I/O performed using IOR.

        Use Cases:
          -- single pool, single client performing continuous read/write/verify
             sequence while failure/rebuild is triggered in another process

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=pool,rebuild
        :avocado: tags=rebuildwithior

        """
        # set params
        targets = self.params.get("targets", "/run/server_config/*")
        ior_timeout = self.params.get("ior_timeout", '/run/ior/*')
        iorflags_write = self.params.get("write_flg", '/run/ior/iorflags/')
        iorflags_read = self.params.get("read_flg", '/run/ior/iorflags/')
        rank_to_kill = self.params.get("rank_to_kill", "/run/ior/*")

        # create pool
        self.create_pool()

        # make sure pool looks good before we start
        checks = {
            "pi_nnodes": len(self.hostlist_servers),
            "pi_ntargets": len(self.hostlist_servers) * targets,
            "pi_ndisabled": 0,
        }
        self.assertTrue(
            self.pool.check_pool_info(**checks),
            "Invalid pool information detected before rebuild")

        self.assertTrue(
            self.pool.check_rebuild_status(rs_errno=0, rs_state=1,
                                           rs_obj_nr=0, rs_rec_nr=0),
            "Invalid pool rebuild info detected before rebuild")

        # perform IOR write before rebuild
        self.ior_cmd.flags.update(iorflags_write)
        self.run_ior_with_pool(timeout=ior_timeout)

        # kill the server
        self.server_managers[0].stop_ranks([rank_to_kill], self.d_log)

        # wait for rebuild to start
        self.pool.wait_for_rebuild(True)

        # wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        # verify the pool information after rebuild
        self.log.info("Verifying pool info after rebuild")
        checks["pi_ndisabled"] = targets
        self.assertTrue(
            self.pool.check_pool_info(**checks),
            "#Invalid pool information detected after rebuild")
        self.log.info("Verifying rebuild info after rebuild")
        self.assertTrue(
            self.pool.check_rebuild_status(rs_errno=0, rs_state=2),
            "#Invalid pool rebuild error number detected after rebuild")

        # perform IOR read after rebuild
        self.ior_cmd.flags.update(iorflags_read)
        self.run_ior_with_pool(create_cont=False, timeout=ior_timeout)
