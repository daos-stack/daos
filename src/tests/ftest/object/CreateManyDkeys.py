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
import ctypes
import traceback
from avocado       import Test

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')
import ServerUtils
import WriteHostFile

from daos_api import DaosContext, DaosPool, DaosContainer, IORequest

class CreateMillionDkeys(Test):
    """
    Tests To Create the Millions Dkeys in same Object.
    """
    def setUp(self):
        with open('../../../.build_vars.json') as json_f:
            build_paths = json.load(json_f)
        basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
        tmp = build_paths['PREFIX'] + '/tmp'
        server_group = self.params.get("server_group",
                                       '/server/',
                                       'daos_server')
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')

        self.hostlist = self.params.get("test_machines", '/run/hosts/*')
        self.hostfile = WriteHostFile.WriteHostFile(self.hostlist, tmp)

        ServerUtils.runServer(self.hostfile, server_group, basepath)

        self.pool = DaosPool(self.context)
        self.pool.create(self.params.get("mode", '/run/pool/createmode/*'),
                         os.geteuid(),
                         os.getegid(),
                         self.params.get("size", '/run/pool/createsize/*'),
                         self.params.get("setname", '/run/pool/createset/*'),
                         None)
        self.pool.connect(1 << 1)
        poh = self.pool.handle
        self.container = DaosContainer(self.context)
        self.container.create(poh)
        self.container.open()

    def tearDown(self):
        if self.hostfile is not None:
            os.remove(self.hostfile)
        self.pool.destroy(1)
        ServerUtils.stopServer()
        ServerUtils.killServer(self.hostlist)

    def test_million_dkeys(self):
        """
        Test millions dkeys in same object
        :avocado: tags=dkeys,regression,vm,large
        """
        ioreq = IORequest(self.context, self.container, None)
        epoch = self.container.get_new_epoch()
        c_epoch = ctypes.c_uint64(epoch)
        no_of_dkeys = self.params.get("number_of_dkeys", '/run/dkeys/')
        try:
            print("Started Writing the Dataset-----------\n")
            for key in range(no_of_dkeys):
                c_dkey = ctypes.create_string_buffer("dkey {0}".format(key))
                c_akey = ctypes.create_string_buffer("akey {0}".format(key))
                c_value = ctypes.create_string_buffer("data {0}".format(key))
                c_size = ctypes.c_size_t(len("data {0}".format(key))+1)
                ioreq.single_insert(c_dkey,
                                    c_akey,
                                    c_value,
                                    c_size,
                                    epoch)

            self.container.commit_epoch(c_epoch)
            print("Started Verification of the Dataset-----------\n")
            for key in range(no_of_dkeys):
                c_dkey = ctypes.create_string_buffer("dkey {0}".format(key))
                c_akey = ctypes.create_string_buffer("akey {0}".format(key))
                val = ioreq.single_fetch(c_dkey,
                                         c_akey,
                                         len("data {0}".format(key))+1,
                                         c_epoch)
                original_data = "data {0}".format(key)
                if original_data != (repr(val.value)[1:-1]):
                    self.fail("ERROR: Data mismatch for dkey = {0}, akey={1}, "
                              "Expected Value={2} and Received Value={3}\n"
                              .format("dkey {0}".format(key),
                                      "akey {0}".format(key),
                                      original_data,
                                      repr(val.value)[1:-1]))

        except ValueError as e:
            print (e)
            print (traceback.format_exc())
            self.fail("Test was expected to pass but it failed.\n")
