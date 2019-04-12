#!/usr/bin/python
'''
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
'''
from __future__ import print_function

import os
import sys
import json
from avocado import Test

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')

import AgentUtils
import ServerUtils
import WriteHostFile
from daos_api import DaosContext

class Dfuse(Test):
    """
    Runs Dfuse test.
    """

    def __init__(self, *args, **kwargs):

        super(Dfuse, self).__init__(*args, **kwargs)

        self.basepath = None
        self.server_group = None
        self.context = None
        self.hostlist_servers = None
        self.hostfile_servers = None
        self.hostlist_clients = None
        self.hostfile_clients = None

    def setUp(self):
        self.agent_sessions = None
        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as build_file:
            build_paths = json.load(build_file)
        self.basepath = os.path.normpath(os.path.join(build_paths['PREFIX'], '..'))

        self.server_group = self.params.get("server_group", '/server/',
                                            'daos_server')

        # setup the DAOS python API
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')

        self.hostlist_servers = self.params.get("test_servers", '/run/hosts/')
        self.hostfile_servers = (
            write_host_file.write_host_file(self.hostlist_servers,
                                            self.workdir))
        print("Host file servers is: {}".format(self.hostfile_servers))

        self.hostlist_clients = self.params.get("test_clients", '/run/hosts/')
        self.hostfile_clients = (
            write_host_file.write_host_file(self.hostlist_clients,
                                            self.workdir))
        print("Host file clients is: {}".format(self.hostfile_clients))

        # start servers
        self.agent_sessions = AgentUtils.run_agent(self.basepath,
                                                   self.hostlist_servers,
                                                   self.hostlist_clients)
        server_utils.run_server(self.hostfile_servers, self.server_group,
                                self.basepath)

        createmode = self.params.get("mode", '/run/poolparams/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/poolparams/')
        createsize = self.params.get("size", '/run/poolparams/')
            
        # setup the pool
        pool = DaosPool(self.context)
        pool.create(createmode, createuid, creategid,
                    createsize, createsetid)
        pool.connect(1 << 1)

        container = DaosContainer(self.context)
        container.create(poh, contuuid)

    def tearDown(self):
        if self.agent_sessions:
            AgentUtils.stop_agent(self.hostlist_clients, self.agent_sessions)
        ServerUtils.stopServer(hosts=self.hostlist_servers)

    def test_dfuse(self):
        """Try and run something over fuse"""
        self.fail("Well, here we are")

    def test_dfuse_optimist(self):
        """Try and run something over fuse"""
        pass
