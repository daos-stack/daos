#!/usr/bin/python
'''
  (C) Copyright 2018 Intel Corporation.

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
import ServerUtils
import WriteHostFile
import IorUtils

from daos_api import DaosContext, DaosPool

class IorOverNvme(Test):
    """
    Tests IOR with NVMe drives
    """
    def setUp(self):
        with open('../../../.build_vars.json') as json_file:
            build_paths = json.load(json_file)
        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
        tmp_path = build_paths['PREFIX'] + '/tmp'
        server_group = self.params.get("name", '/server_config/')
        context = DaosContext(build_paths['PREFIX'] + '/lib/')

        self.hostlist_servers = self.params.get("servers", '/run/hosts/*')
        self.hostfile_servers = WriteHostFile.WriteHostFile(self.hostlist_servers,
                                                            tmp_path)

        self.hostlist_clients = self.params.get("clients", '/run/hosts/*')
        self.hostfile_clients = WriteHostFile.WriteHostFile(self.hostlist_clients,
                                                            tmp_path)

        #This is for NVMe Setup
        self.nvme_parameter = self.params.get("bdev_class", '/server_config/')
        if self.nvme_parameter == "nvme":
            ServerUtils.nvme_setup(self.hostlist_servers)

        #IorUtils.build_ior(self.basepath)
        ServerUtils.runServer(self.hostfile_servers, server_group, self.basepath)

        self.pool = DaosPool(context)
        self.pool.create(self.params.get("mode", '/run/pool/createmode/*'),
                         os.geteuid(),
                         os.getegid(),
                         self.params.get("size", '/run/pool/createsize/*'),
                         self.params.get("setname", '/run/pool/createset/*'),
                         nvme_size = self.params.get("size",
                                                     '/run/pool/createsize/*'))
        self.pool.connect(1 << 1)

    def tearDown(self):
        if self.pool is not None and self.pool.attached:
            self.pool.destroy(1)
        ServerUtils.stopServer()
        ServerUtils.killServer(self.hostlist_servers)
        if self.hostfile_clients is not None:
            os.remove(self.hostfile_clients)
        if self.hostfile_servers is not None:
            os.remove(self.hostfile_servers)

        #For NVMe Cleanup
        if self.nvme_parameter['nvme_mode'] == "Enabled":
            ServerUtils.nvme_cleanup(self.hostlist_servers)

    def test_ior_with_nvme(self):
        """
        Test basic IOR test with NVMe drives.
        :avocado: tags=nvme,ior_nvme
        """
        createsvc = self.params.get("svcn", '/run/pool/createsvc/')
        iteration = self.params.get("iter", '/run/ior/iteration/')
        ior_flags = self.params.get("F", '/run/ior/iorflags/')
        transfer_size = self.params.get("t", '/run/ior/transfersize/')
        record_size = self.params.get("r", '/run/ior/recordsize/')
        segment_count = self.params.get("s", '/run/ior/segmentcount/')
        stripe_count = self.params.get("c", '/run/ior/stripecount/')
        block_size = self.params.get("b", '/run/ior/blocksize/')
        async_io = self.params.get("a", '/run/ior/asyncio/*')
        object_class = self.params.get("o", '/run/ior/objectclass/')

        pool_uuid = self.pool.get_uuid_str()
        rank_list = []
        svc_list = ""

        for i in range(createsvc):
            rank_list.append(int(self.pool.svc.rl_ranks[i]))
            svc_list += str(rank_list[i]) + ":"
        svc_list = svc_list[:-1]

        IorUtils.run_ior(self.hostfile_clients, ior_flags, iteration, block_size, transfer_size,
                         pool_uuid, svc_list, record_size, segment_count, stripe_count,
                         async_io[0], object_class, self.basepath)
