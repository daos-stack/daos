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
class PoolDestroyWithIO(IorTestBase):
    """Rebuild test cases featuring IOR.

    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild using IOR.

    :avocado: recursive
    """

    @skipForTicket("DAOS-5868")
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
          Re-create pool on reamining ranks.

        :avocado: tags=all,pr,hw,medium,ib2,pool,rebuild,pooldestroywithio
        """
        # set params
        targets = self.params.get("targets", "/run/server_config/*/0/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")
        servers_per_host = self.params.get("servers_per_host",
                                           "/run/server_config/*")

        # create pool
        self.create_pool()

        # make sure pool looks good before we start
        checks = {
            "pi_nnodes": len(self.hostlist_servers) * servers_per_host,
            "pi_ntargets": len(self.hostlist_servers) * targets * \
                servers_per_host,
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
        for _ in range(4):
            self.run_ior_with_pool()

        # Kill the server and trigger rebuild
        self.pool.start_rebuild([rank], self.d_log)

        # Wait for rebuild to start. If True just wait for rebuild to start,
        # if False, wait for rebuild to complete.
        self.pool.wait_for_rebuild(True, interval=1)
        self.pool.set_query_data()
        rebuild_status = self.pool.query_data["rebuild"]["status"]
        self.log.info("Pool %s rebuild status:%s\n", self.pool.uuid,
                      rebuild_status)

        if rebuild_status == 'busy':
            # destroy pool during rebuild
            self.pool.destroy()
            self.container = None

        # re-create the pool of full size to verify the space was reclaimed,
        # after re-starting the server on excluded rank
        self.create_pool()
        self.pool.set_query_data()
