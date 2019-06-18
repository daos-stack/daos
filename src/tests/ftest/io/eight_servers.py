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
import distutils.spawn
from apricot import TestWithServers

import write_host_file
import ior_utils
from daos_api import DaosPool, DaosApiError

class EightServers(TestWithServers):
    """
    Test class Description: Runs IOR with 8 servers.
    :avocado: recursive
    """

    def setUp(self):
        super(EightServers, self).setUp()

        # setting client variables
        self.hostfile_clients = (
            write_host_file.write_host_file(self.hostlist_clients, self.workdir,
                                            None))

    def executable(self, iorflags=None):
        """
        Executable function to run ior for sequential and random order
        """

        # parameters used in pool create
        createmode = self.params.get("mode_RW", '/run/pool/createmode/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/pool/createset/')
        createsize = self.params.get("size", '/run/pool/createsize/')
        createsvc = self.params.get("svcn", '/run/pool/createsvc/')

        # ior parameters
        client_processes = self.params.get("np", '/run/ior/clientslots/*')
        iteration = self.params.get("iter", '/run/ior/iteration/')
        block_size = self.params.get("b", '/run/ior/transfersize_blocksize/*/')
        object_class = self.params.get("o", '/run/ior/objectclass/*/')
        transfer_size = self.params.get("t",
                                        '/run/ior/transfersize_blocksize/*/')

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            #print("self.context:{}".format(self.context))
            self.pool.create(createmode, createuid, creategid,
                             createsize, createsetid, None, None, createsvc)

            pool_uuid = self.pool.get_uuid_str()
            svc_list = ""
            for item in range(createsvc):
                svc_list += str(int(self.pool.svc.rl_ranks[item])) + ":"
            svc_list = svc_list[:-1]

            print ("svc_list: {}".format(svc_list))

            ior_utils.run_ior_daos(self.hostfile_clients, iorflags, iteration,
                                   block_size, transfer_size, pool_uuid,
                                   svc_list, object_class, self.basepath,
                                   client_processes)

        except (DaosApiError, ior_utils.IorFailed) as excep:
            print(excep)
            self.fail("Test was expected to pass but it failed.\n")

    def test_sequential(self):
        """
        Jira ID: DAOS-1264
        Test Description: Run IOR with 1,64 and 128 clients config
                          sequentially.
        Use Cases: Different combinations of 1/64/128 Clients and
                   1K/4K/32K/128K/512K/1M transfersize.
        :avocado: tags=ior,eightservers,ior_sequential
        """
        ior_flags = self.params.get("F", '/run/ior/iorflags/sequential/')
        self.executable(ior_flags)

    def test_random(self):
        """
        Jira ID: DAOS-1264
        Test Description: Run IOR with 1,64 and 128 clients config in random
                          order.
        Use Cases: Different combinations of 1/64/128 Clients and
                   1K/4K/32K/128K/512K/1M transfersize.
        :avocado: tags=ior,eightservers,ior_random
        """
        ior_flags = self.params.get("F", '/run/ior/iorflags/random/')
        self.executable(ior_flags)
