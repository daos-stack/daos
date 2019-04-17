"""
  (C) Copyright 2019 Intel Corporation.

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
"""
from __future__ import print_function

import json
import os
import string
import random
import sys

from avocado import Test

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')

import write_host_file
import server_utils
import AgentUtils
from daos_api import (DaosContext, DaosLog, DaosApiError, DaosServer, DaosPool,
                      DaosContainer)

class RebuildContainerCreate(Test):
    """
    Tests container creation while a rebuild is in progress.
    """

    def setUp(self):
        self.hostlist = None

        with open('../../../.build_vars.json') as build_file:
            self.build_paths = json.load(build_file)

        # setup the DAOS python API + client logging
        self.context = DaosContext(self.build_paths['PREFIX'] + '/lib/')
        self.d_log = DaosLog(self.context)

        # init params for pool create
        self.createuid = os.geteuid()
        self.creategid = os.getegid()
        self.createmode = self.params.get("mode",
                                          '/run/poolparams/createmode/')
        self.createsetid = self.params.get("setname",
                                           '/run/poolparams/createset/')
        self.createsize = self.params.get("size",
                                          '/run/poolparams/createsize/')

        # how many objects and records are we creating
        self.objcount = self.params.get("objcount",
                                        '/run/objparams/numobjects/')
        self.reccount = self.params.get("reccount",
                                        '/run/objparams/numrecords/')
        if self.objcount == 0:
            self.reccount = 0

        # which rank to write to and kill
        self.rank = 1

        # how much data to write with each key
        self.size = self.params.get("size", '/run/objparams/datasize/')

        #Start the server + agent
        self.server_group = self.params.get("server_group", '/server/',
                                            'daos_server')
        self.basepath = os.path.normpath(self.build_paths['PREFIX'] + "/../")
        self.hostlist = self.params.get("test_machines", '/run/hosts/')
        self.hostfile = write_host_file.write_host_file(self.hostlist,
                                                        self.workdir)

        self.agent_sessions = AgentUtils.run_agent(self.basepath, self.hostlist)
        server_utils.run_server(self.hostfile, self.server_group, self.basepath)

    def tearDown(self):
        try:
            if self.agent_sessions:
                AgentUtils.stop_agent(self.hostlist, self.agent_sessions)
        finally:
            server_utils.stop_server(hosts=self.hostlist)

    def test_rebuild_cont_create(self):
        """
        Test Description: Test creating a container while rebuild is ongoing.

        :avocado: tags=cont,rebuild,rebuildsimple,rebuildcontainercreate
        """
        try:

            # make ourselves a pool, connect to it
            pool = DaosPool(self.context)
            pool.create(self.createmode, self.createuid, self.creategid,
                        self.createsize, self.createsetid)
            pool.connect(1 << 1)

            pool.pool_query()
            if pool.pool_info.pi_ndisabled != 0:
                self.fail("Number of disabled targets reporting incorrectly.")
            if pool.pool_info.pi_rebuild_st.rs_errno != 0:
                self.fail("Rebuild error but rebuild hasn't run.")
            if pool.pool_info.pi_rebuild_st.rs_obj_nr != 0:
                self.fail("Rebuilt objs not zero.")
            if pool.pool_info.pi_rebuild_st.rs_rec_nr != 0:
                self.fail("Rebuilt recs not zero.")

            # create and open a container
            container = DaosContainer(self.context)
            container.create(pool.handle)
            container.open()

            # fill the pool with enough data such that rebuild runs for a bit
            saved_data = []
            for _objc in range(self.objcount):
                obj = None
                for _recc in range(self.reccount):
                    dkey = (
                        ''.join(random.choice(string.ascii_uppercase +
                                              string.digits) for _ in range(5)))
                    akey = (
                        ''.join(random.choice(string.ascii_uppercase +
                                              string.digits) for _ in range(5)))
                    data = (''.join(random.choice(string.ascii_uppercase +
                                                  string.digits) for _ in
                                    range(self.size)))
                    obj, txn = container.write_an_obj(data, len(data), dkey,
                                                      akey, obj, self.rank,
                                                      obj_cls=16)
                    saved_data.append((obj, dkey, akey, data, txn))

                    # validate the written object
                    data2 = container.read_an_obj(self.size, dkey, akey, obj,
                                                  txn)
                    if data != data2.value:
                        self.fail("Write data 1, read it back, didn't match\n")

            # kill a server
            server = DaosServer(self.context, self.server_group, self.rank)
            server.kill(1)

            # temporarily, the exclude of a failed target must be done manually
            pool.exclude([self.rank])

            # get the pool/rebuild status again
            pool.pool_query()
            if pool.pool_info.pi_rebuild_st.rs_done == 1:
                self.error("rebuild finished too early")
            else:
                # make and open a container while rebuild is active
                rebuild_cont = DaosContainer(self.context)
                rebuild_cont.create(pool.handle)
                rebuild_cont.open()

        except DaosApiError as excep:
            self.fail("Encountered DaosApiError: {0}".format(excep))
