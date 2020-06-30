#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Governments rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from __future__ import print_function

from apricot import skipForTicket
from ior_test_base import IorTestBase


# pylint: disable=too-few-public-methods,too-many-ancestors
class RebuildWithIOR(IorTestBase):
    """Rebuild test cases featuring IOR.

    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild using IOR.

    :avocado: recursive
    """

    @skipForTicket("DAOS-2773")
    def test_rebuild_with_ior(self):
        """Jira ID: DAOS-951.

        Test Description: Trigger a rebuild while I/O is ongoing.
                          I/O performed using IOR.

        Use Cases:
          -- single pool, single client performing continuous read/write/verify
             sequence while failure/rebuild is triggered in another process

        :avocado: tags=all,pr,small,pool,rebuild,rebuildwithior
        """
        # set params
        targets = self.params.get("targets", "/run/server_config/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")

        # ior parameters
        iorflags_write = self.params.get("F", '/run/ior/iorflags/write/')
        iorflags_read = self.params.get("F", '/run/ior/iorflags/read/')
        file1 = "daos:testFile1"
        file2 = "daos:testFile2"

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
            self.pool.check_rebuild_status(rs_errno=0, rs_done=1,
                                           rs_obj_nr=0, rs_rec_nr=0),
            "Invalid pool rebuild info detected before rebuild")

        # perform first set of io using IOR
        self.ior_cmd.flags.update(iorflags_write)
        self.run_ior_with_pool(test_file=file1)

        # Kill the server
        self.pool.start_rebuild([rank], self.d_log)

        # Wait for rebuild to start
        self.pool.wait_for_rebuild(True)

        # Wait for rebuild to complete
        self.pool.wait_for_rebuild(False)

        # Verify the pool information after rebuild
        checks["pi_ndisabled"] = targets
        self.assertTrue(
            self.pool.check_pool_info(**checks),
            "Invalid pool information detected after rebuild")
        self.assertTrue(
            self.pool.check_rebuild_status(rs_errno=0, rs_done=1),
            "Invalid pool rebuild error number detected after rebuild")

        # perform second set of io using IOR
        self.ior_cmd.flags.update(iorflags_write)
        self.run_ior_with_pool(test_file=file2)

        # check data intergrity using ior for both ior runs
        self.ior_cmd.flags.update(iorflags_read)
        self.run_ior_with_pool(test_file=file1)

        self.ior_cmd.flags.update(iorflags_read)
        self.run_ior_with_pool(test_file=file2)
