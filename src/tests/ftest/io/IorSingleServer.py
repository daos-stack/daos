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

import os
import sys
import json

from avocado       import Test

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import AgentUtils
import ServerUtils
import WriteHostFile
import IorUtils
from daos_api import DaosContext, DaosPool, DaosApiError

class IorSingleServer(Test):
    """
    Tests IOR with Single Server config.

    """
    def setUp(self):
        self.agent_sessions = None
        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as f:
            build_paths = json.load(f)
        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")

        self.server_group = self.params.get("server_group", '/server/', 'daos_server')
        self.daosctl = self.basepath + '/install/bin/daosctl'

        # setup the DAOS python API
        self.Context = DaosContext(build_paths['PREFIX'] + '/lib/')
        self.POOL = None

        self.hostlist_servers = self.params.get("test_servers", '/run/hosts/test_machines/*')
        self.hostfile_servers = WriteHostFile.WriteHostFile(self.hostlist_servers, self.workdir)
        print("Host file servers is: {}".format(self.hostfile_servers))

        self.hostlist_clients = self.params.get("clients", '/run/hosts/test_machines/diff_clients/*')
        self.hostfile_clients = WriteHostFile.WriteHostFile(self.hostlist_clients, self.workdir)
        print("Host file clientsis: {}".format(self.hostfile_clients))

        self.agent_sessions = AgentUtils.run_agent(self.basepath,
                                                   self.hostlist_servers,
                                                   self.hostlist_clients)
        ServerUtils.runServer(self.hostfile_servers, self.server_group, self.basepath)

        if int(str(self.name).split("-")[0]) == 1:
            IorUtils.build_ior(self.basepath)

    def tearDown(self):
        try:
            if self.hostfile_clients is not None:
                os.remove(self.hostfile_clients)
            if self.hostfile_servers is not None:
                os.remove(self.hostfile_servers)
            if self.POOL is not None and self.POOL.attached:
                self.POOL.destroy(1)
        finally:
            if self.agent_sessions:
                AgentUtils.stop_agent(self.hostlist_clients,
                                      self.agent_sessions)
            ServerUtils.stopServer(hosts=self.hostlist_servers)

    def test_singleserver(self):
        """
        Test IOR with Single Server config.

        :avocado: tags=ior,singleserver
        """

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/createtests/createmode/*/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/createtests/createset/')
        createsize = self.params.get("size", '/run/createtests/createsize/')
        createsvc = self.params.get("svcn", '/run/createtests/createsvc/')
        iteration = self.params.get("iter", '/run/ior/iteration/')
        ior_flags = self.params.get("F", '/run/ior/iorflags/')
        transfer_size = self.params.get("t", '/run/ior/transfersize/')
        record_size = self.params.get("r", '/run/ior/recordsize/')
        segment_count = self.params.get("s", '/run/ior/segmentcount/')
        stripe_count = self.params.get("c", '/run/ior/stripecount/')
        async_io = self.params.get("a", '/run/ior/asyncio/')
        object_class = self.params.get("o", '/run/ior/objectclass/')

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.POOL = DaosPool(self.Context)
            self.POOL.create(createmode, createuid, creategid,
                             createsize, createsetid, None, None, createsvc)
            pool_uuid = self.POOL.get_uuid_str()
            print ("pool_uuid: {}".format(pool_uuid))
            list = []
            svc_list = ""
            for i in range(createsvc):
                list.append(int(self.POOL.svc.rl_ranks[i]))
                svc_list += str(list[i]) + ":"
            svc_list = svc_list[:-1]

            if len(self.hostlist_clients) == 1:
                block_size = '12g'
            elif len(self.hostlist_clients) == 2:
                block_size = '6g'
            elif len(self.hostlist_clients) == 4:
                block_size = '3g'

            IorUtils.run_ior(self.hostfile_clients, ior_flags, iteration, block_size, transfer_size,
                             pool_uuid, svc_list, record_size, segment_count, stripe_count,
                             async_io, object_class, self.basepath)

        except (DaosApiError, IorUtils.IorFailed) as e:
            self.fail("<Single Server Test FAILED>\n {}".format(e))
