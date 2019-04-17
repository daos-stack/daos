#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

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
'''
from __future__ import print_function

import os
import time
import traceback

from apricot import TestWithoutServers

import agent_utils
import server_utils
import write_host_file
import check_for_pool
import io_utilities
from daos_api import DaosPool, DaosServer, DaosContainer, DaosApiError

class RebuildWithIO(TestWithoutServers):
    """
    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild.
    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        super(RebuildWithIO, self).__init__(*args, **kwargs)
        self.agent_sessions = None

    def test_rebuild_with_io(self):
        """
        Test ID: Rebuild-003

        Test Description: Trigger a rebuild while I/O is ongoing.

        Use Cases:
          -- single pool, single client performing continous read/write/verify
             sequence while failure/rebuild is triggered in another process

        :avocado: tags=pool,rebuild,rebuildwithio
        """

        # the rebuild tests need to redo this stuff each time so not in setup
        # as it usually would be
        server_group = self.params.get("server_group", '/server/',
                                       'daos_server')

        self.hostlist_servers = self.params.get("test_machines", '/run/hosts/')
        hostfile_servers = write_host_file.write_host_file(
            self.hostlist_servers, self.workdir)

        try:
            self.agent_sessions = agent_utils.run_agent(self.basepath,
                                                        self.hostlist_servers)
            server_utils.run_server(hostfile_servers, server_group,
                                    self.basepath)

            # use the uid/gid of the user running the test, these should
            # be perfectly valid
            createuid = os.geteuid()
            creategid = os.getegid()

            # parameters used in pool create that are in yaml
            createmode = self.params.get("mode", '/run/testparams/createmode/')
            createsetid = self.params.get("setname",
                                          '/run/testparams/createset/')
            createsize = self.params.get("size", '/run/testparams/createsize/')

            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(self.context)
            pool.create(createmode, createuid, creategid,
                        createsize, createsetid, None)
            pool.connect(1 << 1)
            container = DaosContainer(self.context)
            container.create(pool.handle)
            container.open()

            # get pool status and make sure it all looks good before we start
            pool.pool_query()
            if pool.pool_info.pi_ndisabled != 0:
                self.fail("Number of disabled targets reporting incorrectly.\n")
            if pool.pool_info.pi_rebuild_st.rs_errno != 0:
                self.fail("Rebuild error but rebuild hasn't run.\n")
            if pool.pool_info.pi_rebuild_st.rs_done != 1:
                self.fail("Rebuild is running but device hasn't failed yet.\n")
            if pool.pool_info.pi_rebuild_st.rs_obj_nr != 0:
                self.fail("Rebuilt objs not zero.\n")
            if pool.pool_info.pi_rebuild_st.rs_rec_nr != 0:
                self.fail("Rebuilt recs not zero.\n")
            dummy_pool_version = pool.pool_info.pi_rebuild_st.rs_version

            # do I/O for 30 seconds
            dummy_bw = io_utilities.continuous_io(container, 30)

            # trigger the rebuild
            rank = self.params.get("rank", '/run/testparams/ranks/*')
            server = DaosServer(self.context, server_group, rank)
            server.kill(1)
            pool.exclude([rank])

            # do another 30 seconds of I/O,
            # waiting for some improvements in server bootstrap
            # at which point we can move the I/O to a separate client and
            # really pound it with I/O
            dummy_bw = io_utilities.continuous_io(container, 30)

            # wait for the rebuild to finish
            while True:
                pool.pool_query()
                if pool.pool_info.pi_rebuild_st.rs_done == 1:
                    break
                else:
                    time.sleep(2)

            # check rebuild statistics
            if pool.pool_info.pi_ndisabled != 1:
                self.fail("Number of disabled targets reporting incorrectly: {}"
                          .format(pool.pool_info.pi_ndisabled))
            if pool.pool_info.pi_rebuild_st.rs_errno != 0:
                self.fail("Rebuild error reported: {}".format(
                    pool.pool_info.pi_rebuild_st.rs_errno))
            if pool.pool_info.pi_rebuild_st.rs_obj_nr <= 0:
                self.fail("No objects have been rebuilt.")
            if pool.pool_info.pi_rebuild_st.rs_rec_nr <= 0:
                self.fail("No records have been rebuilt.")

        except (ValueError, DaosApiError) as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")

        finally:
            # wait for the I/O process to finish
            try:
                server_utils.stop_server(hosts=self.hostlist_servers)
                os.remove(hostfile_servers)
                # really make sure everything is gone
                check_for_pool.cleanup_pools(self.hostlist_servers)
            finally:
                if self.agent_sessions:
                    agent_utils.stop_agent(self.hostlist_servers,
                                           self.agent_sessions)
                server_utils.kill_server(self.hostlist_servers)
