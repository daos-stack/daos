#!/usr/bin/python
'''
  (C) Copyright 2017-2019 Intel Corporation.

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
import traceback
import sys
import json

from apricot       import Test

import ServerUtils
import WriteHostFile
from daos_api import DaosPool, DaosContext, DaosApiError

class SimpleCreateDeleteTest(Test):
    """
    Tests DAOS pool creation, trying both valid and invalid parameters.

    :avocado: recursive
    """
    # super wasteful since its doing this for every variation
    def setUp(self):
        self.pool = None
        self.hostlist = None

        with open('../../../.build_vars.json') as filep:
            build_paths = json.load(filep)
        basepath = os.path.normpath(build_paths['PREFIX']  + "/../")

        self.context = DaosContext(build_paths['PREFIX'] + '/lib/')
        self.hostlist = self.params.get("test_machines", '/run/hosts/')
        hostfile = WriteHostFile.WriteHostFile(self.hostlist, self.workdir)

        server_group = self.params.get("server_group", '/server/', 'daos_server')

        ServerUtils.runServer(self.hostfile, server_group, basepath)

    def tearDown(self):
        try:
            if self.pool is not None and self.pool.attached:
                self.pool.destroy(1)
        finally:
            ServerUtils.stopServer(hosts=self.hostlist)

    def test_create(self):
        """
        Test basic pool creation.

        :avocado: tags=pool,poolcreate,simplecreate
        """
        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []
        modelist = self.params.get("mode", '/run/tests/modes/*')
        mode = modelist[0]
        expected_for_param.append(modelist[1])

        uidlist = self.params.get("uid", '/run/tests/uids/*', os.geteuid())
        if uidlist[0] == 'valid':
            uid = os.geteuid()
        else:
            uid = uidlist[0]
        expected_for_param.append(uidlist[1])

        gidlist = self.params.get("gid", '/run/tests/gids/*', os.getegid())
        if gidlist[0] == 'valid':
            gid = os.getegid()
        else:
            gid = gidlist[0]
        expected_for_param.append(gidlist[1])

        setidlist = self.params.get("setname", '/run/tests/setnames/*')
        setid = setidlist[0]
        expected_for_param.append(setidlist[1])

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        try:
            self.pool = DaosPool(self.context)
            self.pool.create(mode, uid, gid, 1073741824, setid)
            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as exc:
            print (exc)
            print(traceback.format_exc())
            if expected_result not in ['FAIL']:
                self.fail("Test was expected to pass but it failed.\n")
