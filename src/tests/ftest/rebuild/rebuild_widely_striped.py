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

import threading
import time

from apricot import skipForTicket
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase

# pylint: disable=too-few-public-methods,too-many-ancestors
class RebuildWidelyStriped(MdtestBase):
    """Rebuild test cases featuring mdtest.

    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild using IOR.

    :avocado: recursive
    """

#    @skipForTicket("DAOS-5868")
    def test_rebuild_widely_striped(self):
        """Jira ID: DAOS-3795/DAOS-3796.

        Test Description: Verify rebuild for widely striped object using
                          mdtest.

        Use Cases:
          Create pool and container.
          Use mdtest to create 32K files of size 4K with 3-way
          replication.
          Stop one server, let rebuild start and complete.
          Destroy container and create a new one.
          Use mdtest to create 32K files of size 4K with 3-way
          replication.
          Stop one more server in the middle of mdtest. Let rebuild to complete.
          Allow mdtest to complete.
          Destroy container and create a new one.
          Use mdtest to create 32K files of size 4K with 3-way
          replication.
          Stop 2 servers in the middle of mdtest. Let rebuild to complete.
          Allow mdtest to complete.

        :avocado: tags=all,pr,small,pool,rebuild,widelystriped
        """
        # set params
        targets = self.params.get("targets", "/run/server_config/*/0/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")
        servers_per_host = self.params.get("servers_per_host",
                                           "/run/server_config/*")

        # create pool
        self.add_pool(connect=False)

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



        # run mdtest to write 32K files of size 4K
#        self.execute_mdtest()

        thread = threading.Thread(target=self.execute_mdtest)
#        threads.append(thread)
#        thread.daemon = True
        thread.start()
        time.sleep(2)

#        thread.join()

        # Kill the server and trigger rebuild
        self.pool.start_rebuild([rank], self.d_log)

        # Wait for rebuild to start. If True just wait for rebuild to start,
        # if False, wait for rebuild to complete.
        self.pool.wait_for_rebuild(False, interval=1)

        thread.join()

        self.pool.set_query_data()

        # destroy pool during rebuild
#        self.pool.destroy()
#        self.container = None

        # re-create the pool of full size to verify the space was reclaimed,
        # after re-starting the server on excluded rank
#        self.create_pool()
#        self.pool.set_query_data()
