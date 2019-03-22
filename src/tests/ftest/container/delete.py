#!/usr/bin/python
"""
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
"""

import json
import os
import sys
import time
import traceback
import uuid

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')

import server_utils
import write_host_file
from avocado import Test
from daos_api import DaosApiError, DaosContainer, DaosContext, DaosLog, DaosPool

class DeleteContainerTest(Test):
    """
    Tests DAOS container delete and close.
    """
    def setUp(self):

        # get paths from the build_vars generated by build
        self.hostlist = None
        with open('../../../.build_vars.json') as build_file:
            build_paths = json.load(build_file)
        self.basepath = os.path.normpath(build_paths['PREFIX'] + "/../")
        self.server_group = self.params.get("server_group", '/server/',
                                            'daos_server')

        # parameters used in pool create
        self.createmode = self.params.get("mode",
                                          '/run/createtests/createmode/')
        self.createuid = os.geteuid()
        self.creategid = os.getegid()
        self.createsetid = self.params.get("setname",
                                           '/run/createtests/createset/')
        self.createsize = self.params.get("size",
                                          '/run/createtests/createsize/')

        # setup the DAOS python API
        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')
        self.pool = None
        self.container = None

        # hostlist and logging
        self.d_log = DaosLog(self.context)
        self.hostlist = self.params.get("test_machines", '/run/hosts/*')
        self.hostfile = write_host_file.write_host_file(self.hostlist,
                                                        self.workdir)
        server_utils.run_server(self.hostfile, self.server_group, self.basepath)

    def tearDown(self):
        server_utils.stop_server(hosts=self.hostlist)

    def test_container_delete(self):
        """
        Test basic container delete
        :avocado: tags=regression,cont,vm,contdelete
        """
        expected_for_param = []
        uuidlist = self.params.get("uuid",
                                   '/run/createtests/ContainerUUIDS/*/')
        cont_uuid = uuidlist[0]
        expected_for_param.append(uuidlist[1])

        pohlist = self.params.get("poh", '/run/createtests/PoolHandles/*/')
        poh = pohlist[0]
        expected_for_param.append(pohlist[1])

        openlist = self.params.get("opened",
                                   "/run/createtests/ConnectionOpened/*/")
        opened = openlist[0]
        expected_for_param.append(openlist[1])

        forcelist = self.params.get("force", "/run/createtests/ForceDestroy/*/")
        force = forcelist[0]
        expected_for_param.append(forcelist[1])

        if force >= 1:
            self.cancel("Force >= 1 blocked by issue described in "
                        "https://jira.hpdd.intel.com/browse/DAOS-689")

        if force == 0:
            self.cancel("Force = 0 blocked by "
                        "https://jira.hpdd.intel.com/browse/DAOS-1935")

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            self.pool = DaosPool(self.context)
            self.pool.create(self.createmode, self.createuid, self.creategid,
                             self.createsize, self.createsetid, None)

            # need a connection to create container
            self.pool.connect(1 << 1)
            self.container = DaosContainer(self.context)

            # create should always work (testing destroy)
            if not cont_uuid == 'INVALID':
                cont_uuid = uuid.UUID(uuidlist[0])
                self.container.create(self.pool.handle, cont_uuid)
            else:
                self.container.create(self.pool.handle)

            # Opens the container if required
            if opened:
                self.container.open(self.pool.handle)

            # wait a few seconds and then attempts to destroy container
            time.sleep(5)
            if poh == 'VALID':
                poh = self.pool.handle

            # if container is INVALID, overwrite with non existing UUID
            if cont_uuid == 'INVALID':
                cont_uuid = uuid.uuid4()

            self.container.destroy(force=force, poh=poh, con_uuid=cont_uuid)
            self.container = None

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            self.d_log.error(excep)
            self.d_log.error(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")

        finally:
            # clean up the pool
            if self.pool is not None:
                self.pool.destroy(1)
                self.pool = None
