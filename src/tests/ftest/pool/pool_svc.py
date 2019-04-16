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

import os
import traceback
import sys
import json

from avocado import Test
from avocado.utils import process

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import AgentUtils
import server_utils
import write_host_file
from daos_api import DaosContext, DaosPool, DaosServer, DaosApiError

class PoolSvc(Test):
    """
    Tests svc argument while pool create.
    """
    def setUp(self):
        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as build_file:
            build_paths = json.load(build_file)
        self.basepath = os.path.normpath(build_paths['PREFIX'] + "/../")

        self.server_group = self.params.get("server_group", '/server/',
                                            'daos_server')
        self.daosctl = self.basepath + '/install/bin/daosctl'

        # setup the DAOS python API
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')
        self.pool = None

        self.hostfile = None
        self.hostlist = self.params.get("test_machines", '/run/hosts/*')
        self.hostfile = write_host_file.write_host_file(self.hostlist,
                                                        self.workdir)
        print("Host file is: {}".format(self.hostfile))

        self.agent_sessions = AgentUtils.run_agent(self.basepath, self.hostlist)
        server_utils.run_server(self.hostfile, self.server_group, self.basepath)

    def tearDown(self):
        try:
            if self.pool is not None and self.pool.attached:
                self.pool.destroy(1)
        finally:
            if self.agent_sessions:
                AgentUtils.stop_agent(self.hostlist, self.agent_sessions)
            server_utils.stop_server(hosts=self.hostlist)

    def test_poolsvc(self):
        """
        Test svc arg during pool create.

        :avocado: tags=pool,svc
        """

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/createtests/createmode/*/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/createtests/createset/')
        createsize = self.params.get("size", '/run/createtests/createsize/')
        createsvc = self.params.get("svc", '/run/createtests/createsvc/*/')

        expected_result = createsvc[1]

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None, None, createsvc[0])
            self.pool.connect(1 << 1)

            # checking returned rank list for server more than 1
            i = 0
            while (
                    int(self.pool.svc.rl_ranks[i]) > 0 and
                    int(self.pool.svc.rl_ranks[i]) <= createsvc[0] and
                    int(self.pool.svc.rl_ranks[i]) != 999999
            ):
                i += 1
            if i != createsvc[0]:
                self.fail("Length of Returned Rank list is not equal to "
                          "the number of Pool Service members.\n")
            rank_list = []
            for j in range(createsvc[0]):
                rank_list.append(int(self.pool.svc.rl_ranks[j]))
                if len(rank_list) != len(set(rank_list)):
                    self.fail("Duplicate values in returned rank list")

            if createsvc[0] == 3:
                self.pool.disconnect()
                cmd = ('{0} kill-leader  --uuid={1}'
                       .format(self.daosctl, self.pool.get_uuid_str()))
                process.system(cmd)
                self.pool.connect(1 << 1)
                self.pool.disconnect()
                server = DaosServer(self.context, self.server_group, 2)
                server.kill(1)
                self.pool.exclude([2])
                self.pool.connect(1 << 1)

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
