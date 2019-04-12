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
import uuid

from avocado import Test

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')

import AgentUtils
import server_utils
import write_host_file
from daos_api import DaosContext, DaosPool, DaosContainer, DaosApiError

class CreateContainerTest(Test):
    """
    Tests DAOS container create.
    """
    def setUp(self):
        self.agent_sessions = None
        self.hostlist = None

        # get paths from the build_vars generated by build
        with open('../../../.build_vars.json') as build_file:
            build_paths = json.load(build_file)
        self.basepath = os.path.normpath(build_paths['PREFIX'] + "/../")
        self.server_group = self.params.get("name", '/server_config/')

        self.container = None

        # setup the DAOS python API
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')

        self.hostlist = self.params.get("test_machines", '/run/hosts/*')
        hostfile = write_host_file.write_host_file(self.hostlist, self.workdir)

        self.agent_sessions = AgentUtils.run_agent(self.basepath, self.hostlist)
        server_utils.run_server(hostfile, self.server_group, self.basepath)

    def tearDown(self):
        if self.agent_sessions:
            AgentUtils.stop_agent(self.hostlist, self.agent_sessions)
        server_utils.stop_server(None, self.hostlist)

    def test_container_create(self):
        """
        Test ID: DAOS-689

        Test Description: valid and invalid container creation and close.

        :avocado: tags=regression,cont,contcreate
        """

        pool = None
        contuuid = None
        expected_results = []

        try:
            # initialize a python pool object then create the underlying
            # daos storage
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

            # maybe use the good handle, maybe not
            handleparam = self.params.get("handle", '/run/poolhandle/*')
            if handleparam == 'VALID':
                poh = pool.handle
            else:
                poh = handleparam
                expected_results.append('FAIL')

            # maybe use a good UUID, maybe not
            uuidparam = self.params.get("uuid", "/uuids/*")
            expected_results.append(uuidparam[1])
            if uuidparam[0] == 'NULLPTR':
                self.cancel("skipping this test until DAOS-2043 is fixed")
                contuuid = 'NULLPTR'
            else:
                contuuid = uuid.UUID(uuidparam[0])

            should_fail = False
            for result in expected_results:
                if result == 'FAIL':
                    should_fail = True
                    break

            self.container = DaosContainer(self.context)
            self.container.create(poh, contuuid)

            # check UUID is the specified one
            if (uuidparam[0]).upper() != self.container.get_uuid_str().upper():
                print("uuidparam[0] is {}, uuid_str is {}".format(
                    uuidparam[0], self.container.get_uuid_str()))
                self.fail("Container UUID differs from specified at create\n")

            if should_fail:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if not should_fail:
                self.fail("Test was expected to pass but it failed.\n")
        finally:
            if pool is not None:
                pool.disconnect()
                pool.destroy(1)
