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
import sys
import json
from avocado import Test

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import AgentUtils
import server_utils
import write_host_file
import ior_utils
from daos_api import DaosContext, DaosPool, DaosApiError

class SegCount(Test):
    """
    Test class Description: Runs IOR with different segment counts.

    """

    def __init__(self, *args, **kwargs):

        super(SegCount, self).__init__(*args, **kwargs)

        self.basepath = None
        self.context = None
        self.pool = None
        self.slots = None
        self.hostlist_servers = None
        self.hostfile_clients = None

    def setUp(self):
        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as build_file:
            build_paths = json.load(build_file)
        self.basepath = os.path.normpath(build_paths['PREFIX'] + "/../")

        self.server_group = self.params.get("server_group", '/server/',
                                            'daos_server')

        # setup the DAOS python API
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')

        self.hostlist_servers = self.params.get("test_servers", '/run/hosts/*')
        hostfile_servers = (
            write_host_file.write_host_file(self.hostlist_servers,
                                            self.workdir))
        print("Host file servers is: {}".format(hostfile_servers))

        hostlist_clients = self.params.get("test_clients", '/run/hosts/*')
        self.slots = self.params.get("slots", '/run/ior/clientslots/*')
        self.hostfile_clients = (
            write_host_file.write_host_file(hostlist_clients, self.workdir,
                                            self.slots))
        print("Host file clients is: {}".format(self.hostfile_clients))

        self.agent_sessions = AgentUtils.run_agent(self.basepath,
                                                   self.hostlist_servers,
                                                   self.hostlist_clients)
        server_utils.run_server(hostfile_servers, self.server_group,
                                self.basepath)

        if int(str(self.name).split("-")[0]) == 1:
            ior_utils.build_ior(self.basepath)

    def tearDown(self):
        try:
            if self.pool is not None and self.pool.attached:
                self.pool.destroy(1)
        finally:
            if self.agent_sessions:
                AgentUtils.stop_agent(self.hostlist_clients,
                                      self.agent_sessions)
            server_utils.stop_server(hosts=self.hostlist_servers)

    def test_segcount(self):
        """
        Test ID: DAOS-1782
        Test Description: Run IOR with 32,64 and 128 clients with different
                          segment counts.
        Use Cases: Different combinations of 32/64/128 Clients, 8b/1k/4k
                   record size, 1k/4k/1m/8m transfersize and stripesize
                   and 16 async io.
        :avocado: tags=ior,eightservers,ior_segcount,performance
        """

        # parameters used in pool create
        createmode = self.params.get("mode", '/run/pool/createmode/*/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/pool/createset/')
        createsize = self.params.get("size", '/run/pool/createsize/')
        createsvc = self.params.get("svcn", '/run/pool/createsvc/')
        iteration = self.params.get("iter", '/run/ior/iteration/')
        ior_flags = self.params.get("F", '/run/ior/iorflags/')
        stripe_count = self.params.get("c", '/run/ior/stripecount/')
        async_io = self.params.get("a", '/run/ior/asyncio/')
        object_class = self.params.get("o", '/run/ior/objectclass/*/')
        record_size = self.params.get("r", '/run/ior/recordsize/*')
        block_size = (
            self.params.get("b",
                            '/run/ior/blocksize_transfersize_stripesize/*/'))
        transfer_size = (
            self.params.get("t",
                            '/run/ior/blocksize_transfersize_stripesize/*/'))
        stripe_size = (
            self.params.get("s",
                            '/run/ior/blocksize_transfersize_stripesize/*/'))


        if block_size == '4k' and self.slots == 16:
            segment_count = 491500
        elif block_size == '4k' and self.slots == 32:
            segment_count = 245750
        elif block_size == '4k' and self.slots == 64:
            segment_count = 122875
        elif block_size == '1m' and self.slots == 16:
            segment_count = 1920
        elif block_size == '1m' and self.slots == 32:
            segment_count = 960
        elif block_size == '1m' and self.slots == 64:
            segment_count = 480
        elif block_size == '4m' and self.slots == 16:
            segment_count = 480
        elif block_size == '4m' and self.slots == 32:
            segment_count = 240
        elif block_size == '4m' and self.slots == 64:
            segment_count = 120

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None, None, createsvc)

            pool_uuid = self.pool.get_uuid_str()
            svc_list = ""
            for i in range(createsvc):
                svc_list += str(int(self.pool.svc.rl_ranks[i])) + ":"
            svc_list = svc_list[:-1]

            ior_utils.run_ior(self.hostfile_clients, ior_flags, iteration,
                              block_size, transfer_size, pool_uuid, svc_list,
                              record_size, stripe_size, stripe_count, async_io,
                              object_class, self.basepath, self.slots,
                              segment_count)

        except (ior_utils.IorFailed, DaosApiError) as excep:
            self.fail("<SegCount Test FAILED>.{}".format(excep))
