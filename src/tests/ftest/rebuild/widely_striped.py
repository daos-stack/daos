#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

    SPDX-License-Identifier: BSD-2-Clause-Patent
"""


import time
from mdtest_test_base import MdtestBase


# pylint: disable=too-few-public-methods,too-many-ancestors
class RbldWidelyStriped(MdtestBase):
    """Rebuild test cases featuring mdtest.

    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild using IOR.

    :avocado: recursive
    """

    def test_rebuild_widely_striped(self):
        """Jira ID: DAOS-3795/DAOS-3796.

        Test Description: Verify rebuild for widely striped object using
                          mdtest.

        Use Cases:
          Create pool and container.
          Use mdtest to create 120K files of size 32K with 3-way
          replication.
          Stop one server, let rebuild start and complete.
          Destroy container and create a new one.
          Use mdtest to create 120K files of size 32K with 3-way
          replication.
          Stop one more server in the middle of mdtest. Let rebuild to complete.
          Allow mdtest to complete.
          Destroy container and create a new one.
          Use mdtest to create 120K files of size 32K with 3-way
          replication.
          Stop 2 servers in the middle of mdtest. Let rebuild to complete.
          Allow mdtest to complete.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=rebuild
        :avocado: tags=rebuild_widely_striped
        """
        # set params
        targets = self.params.get("targets", "/run/server_config/*")
        ranks_to_kill = self.params.get("ranks_to_kill", "/run/testparams/*")

        # create pool
        self.add_pool(connect=False)

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

        # create 1st container
        self.add_container(self.pool)
        # start 1st mdtest run and let it complete
        self.execute_mdtest()
        # Kill rank[6] and wait for rebuild to complete
        self.server_managers[0].stop_ranks(ranks_to_kill[0], self.d_log, force=True)
        self.pool.wait_for_rebuild(False, interval=1)

        # create 2nd container
        self.add_container(self.pool)

        # start 2nd mdtest job in the background
        self.subprocess = True
        self.execute_mdtest()

        # Kill rank[5] in the middle of mdtest run and wait for rebuild to complete
        time.sleep(3)
        self.server_managers[0].stop_ranks(ranks_to_kill[1], self.d_log, force=True)
        self.pool.wait_for_rebuild(False, interval=1)

        # wait for mdtest to complete successfully
        mdtest_returncode = self.job_manager.process.wait()
        if mdtest_returncode != 0:
            self.fail("mdtest failed")

        # create 3rd container
        self.add_container(self.pool)

        # start 3rd mdtest job in the background
        self.execute_mdtest()

        # Kill 2 server ranks [3,4] during mdtest and wait for rebuild to complete
        time.sleep(3)
        self.server_managers[0].stop_ranks(ranks_to_kill[2], self.d_log, force=True)
        self.pool.wait_for_rebuild(False, interval=1)

        # wait for mdtest to complete successfully
        mdtest_returncode = self.job_manager.process.wait()
        if mdtest_returncode != 0:
            self.fail("mdtest failed")
