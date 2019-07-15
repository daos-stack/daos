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
from apricot import TestWithServers

import write_host_file
import ior_utils
from daos_api import DaosPool, DaosApiError

class FourServers(TestWithServers):
    """
    Test class Description: Runs IOR with four servers.
    :avocado: recursive
    """
    def setUp(self):
        super(FourServers, self).setUp()

        #set client variables
        self.hostfile_clients = (
            write_host_file.write_host_file(self.hostlist_clients,
                                            self.workdir, None))

    def test_fourservers(self):
        """
        Jira ID: DAOS-1263
        Test Description: Test IOR with four servers.
        Use Cases: Different combinations of 1/64/128 Clients,
                   1K/4K/32K/128K/512K/1M transfer size.
        :avocado: tags=ior,fourservers
        """

        # parameters used in pool create
        createmode = self.params.get("mode_RW", '/run/pool/createmode/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/pool/createset/')
        createsize = self.params.get("size", '/run/pool/createsize/')
        createsvc = self.params.get("svcn", '/run/pool/createsvc/')

        # ior parameters
        iteration = self.params.get("iter", '/run/ior/iteration/')
        client_processes = self.params.get("np", '/run/ior/clientslots/*')
        ior_flags = self.params.get("F", '/run/ior/iorflags/')
        transfer_size = self.params.get("t",
                                        '/run/ior/transfersize_blocksize/*/')
        block_size = self.params.get("b",
                                     '/run/ior/transfersize_blocksize/*/')
        object_class = self.params.get("o", '/run/ior/objectclass/')

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(createmode, createuid, creategid, createsize,
                             createsetid, None, None, createsvc)

            pool_uuid = self.pool.get_uuid_str()
            tmp_rank_list = []
            svc_list = ""
            for i in range(createsvc):
                tmp_rank_list.append(int(self.pool.svc.rl_ranks[i]))
                svc_list += str(tmp_rank_list[i]) + ":"
            svc_list = svc_list[:-1]

            ior_utils.run_ior_daos(self.hostfile_clients, ior_flags, iteration,
                                   block_size, transfer_size, pool_uuid,
                                   svc_list, object_class, self.basepath,
                                   client_processes)

        except (DaosApiError, ior_utils.IorFailed) as excep:
            self.fail("<FourServers Test run Failed>\n {}".format(excep))
