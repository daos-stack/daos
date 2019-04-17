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
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function

import time
import traceback
import subprocess
from apricot import TestWithServers

from daos_api import DaosPool, DaosServer, DaosApiError

class RebuildNoCap(TestWithServers):

    """
    Test Class Description:
    This class contains tests for pool rebuild.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        super(RebuildNoCap, self).__init__(*args, **kwargs)
        self.build_paths = []
        self.server_group = ""
        self.context = None
        self.pool = None

    def setUp(self):
        super(RebuildNoCap, self).setUp()
        # create a pool to test with
        createmode = self.params.get("mode", '/run/pool/createmode/')
        createuid = self.params.get("uid", '/run/pool/createuid/')
        creategid = self.params.get("gid", '/run/pool/creategid/')
        createsetid = self.params.get("setname", '/run/pool/createset/')
        createsize = self.params.get("size", '/run/pool/createsize/')
        self.pool = DaosPool(self.context)
        self.pool.create(createmode, createuid, creategid, createsize,
                         createsetid)
        uuid = self.pool.get_uuid_str()

        time.sleep(2)

        # stuff some bogus data into the pool
        how_many_bytes = long(self.params.get("datasize",
                                              '/run/testparams/datatowrite/'))
        exepath = self.prefix +\
                 "/../src/tests/ftest/util/write_some_data.py"
        cmd = "export DAOS_POOL={0}; export DAOS_SVCL=1; mpirun"\
              " --np 1 --host {1} {2} {3} testfile".format(
                  uuid, self.hostlist_servers[0], exepath, how_many_bytes)
        subprocess.call(cmd, shell=True)

    def tearDown(self):
        """ cleanup after the test """

        try:
            if self.pool:
                self.pool.destroy(1)
        finally:
            super(RebuildNoCap, self).tearDown()


    def test_rebuild_no_capacity(self):
        """
        :avocado: tags=pool,rebuild,nocap
        """
        try:
            print("\nsetup complete, starting test\n")

            # create a server object that references on of our pool target hosts
            # and then kill it
            svr_to_kill = int(self.params.get("rank_to_kill",
                                              '/run/testparams/ranks/'))
            d_server = DaosServer(self.context, bytes(self.server_group),
                                  svr_to_kill)

            time.sleep(1)
            d_server.kill(1)

            # exclude the target from the dead server
            self.pool.exclude([svr_to_kill])

            # exclude should trigger rebuild, check
            self.pool.connect(1 << 1)
            status = self.pool.pool_query()
            if not status.pi_ntargets == len(self.hostlist_servers):
                self.fail("target count wrong.\n")
            if not status.pi_ndisabled == 1:
                self.fail("disabled target count wrong.\n")

            # the pool should be too full to start a rebuild so
            # expecting an error
            # not sure yet specifically what error
            if status.pi_rebuild_st.rs_errno == 0:
                self.fail("expecting rebuild to fail but it didn't.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")
