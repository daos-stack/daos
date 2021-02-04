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

from mdtest_test_base import MdtestBase


# pylint: disable=too-few-public-methods,too-many-ancestors
# pylint: disable=attribute-defined-outside-init
class RebuildWidelyStriped(MdtestBase):
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
        :avocado: tags=rebuild,widelystriped
        """
        # set params
        targets = self.params.get("targets", "/run/server_config/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")
        self.dmg = self.get_dmg_command()

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
            self.pool.check_rebuild_status(rs_errno=0, rs_done=1,
                                           rs_obj_nr=0, rs_rec_nr=0),
            "Invalid pool rebuild info detected before rebuild")


        # create 1st container
        self.add_container(self.pool)
        # start 1st mdtest run and let it complete
        self.execute_mdtest()
        # Kill rank[7] and wait for rebuild to complete
        self.pool.start_rebuild([rank[0]], self.d_log)
        self.pool.wait_for_rebuild(False, interval=1)
        # destroy container
        self.container.destroy()


        # create 2nd container
        self.add_container(self.pool)
        # start 1st mdtest job
        thread = threading.Thread(target=self.execute_mdtest)
        thread.start()
        time.sleep(3)

        # Kill rank[6] in the middle of mdtest run and
        # wait for rebuild to complete
        self.pool.start_rebuild([rank[1]], self.d_log)
        self.pool.wait_for_rebuild(False, interval=1)
        # wait for mdtest to complete
        thread.join()

        # destroy container and pool
        self.container.destroy()

        # re-create the pool and container
        self.add_container(self.pool)

        # start 2nd mdtest job
        thread = threading.Thread(target=self.execute_mdtest)
        thread.start()
        time.sleep(3)

        # Kill 2 server ranks [5,6]
        self.pool.start_rebuild([rank[2]], self.d_log)
        self.pool.wait_for_rebuild(False, interval=1)
        # wait for mdtest to complete
        thread.join()
