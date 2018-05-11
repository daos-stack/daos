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
import time
import traceback
import sys
import json
import ctypes

from avocado       import Test
from avocado       import main
from avocado.utils import process
from avocado.utils import git

sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./util')
sys.path.append('./../../utils/py')
import ServerUtils
import CheckForPool
from daos_api import DaosContext
from daos_api import DaosPool
from daos_api import RankList

class BadExcludeTest(Test):
    """
    Tests target exclude calls passing NULL and otherwise inappropriate
    parameters.  This can't be done with daosctl, need to use the python API.

    :avocado: tags=pool,badparam,badexclude
    """

    def setUp(self):
       global hostfile
       global basepath

       basepath = os.path.normpath(os.getcwd() + "../../../../")

       hostfile = basepath + self.params.get("hostfile",'/files/local/',
                                             'rubbish')
       server_group = self.params.get("server_group",'/server/','daos_server')

       ServerUtils.runServer(hostfile, server_group, basepath)

       time.sleep(2)

    def tearDown(self):
       ServerUtils.stopServer()

    def test_exclude(self):
        """
        Pass bad parameters to pool connect

        :avocado: tags=pool,poolexclude,badparam,badexclude
        """
        global basepath

        # parameters used in pool create
        createmode = self.params.get("mode",'/run/excludetests/createmode/')
        createuid  = self.params.get("uid",'/run/excludetests/createuid/')
        creategid  = self.params.get("gid",'/run/excludetests/creategid/')
        createsetid = self.params.get("setname",'/run/excludetests/createset/')
        createsize  = self.params.get("size",'/run/excludetests/createsize/')

        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        tgtlist = self.params.get("ranklist",'/run/excludetests/tgtlist/*/')
        targets = []
        targets.append(tgtlist[0])
        expected_for_param.append(tgtlist[1])

        svclist = self.params.get("ranklist",'/run/excludetests/svrlist/*/')
        svc = svclist[0]
        expected_for_param.append(svclist[1])

        setlist = self.params.get("setname",
                                  '/run/excludetests/connectsetnames/*/')
        connectset = setlist[0]
        expected_for_param.append(setlist[1])

        uuidlist = self.params.get("uuid",'/run/excludetests/UUID/*/')
        excludeuuid = uuidlist[0]
        expected_for_param.append(uuidlist[1])

        # if any parameter is FAIL then the test should FAIL, in this test
        # virtually everyone should FAIL since we are testing bad parameters
        expected_result = 'PASS'
        for result in expected_for_param:
               if result == 'FAIL':
                      expected_result = 'FAIL'
                      break

        try:
            # setup the DAOS python API
            with open('../../../.build_vars.json') as f:
                data = json.load(f)
            CONTEXT = DaosContext(data['PREFIX'] + '/lib/')

            # initialize a python pool object then create the underlying
            # daos storage
            POOL = DaosPool(CONTEXT)
            POOL.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            # trash the the pool service rank list
            #if not svc == 'VALID':
            #    rl_ranks = ctypes.POINTER(ctypes.c_uint)()
            #    POOL.svc = RankList(rl_ranks, 1);

            # trash the pool group value
            #if connectset == None:
            #    POOL.group = None

            # trash the UUID value in various ways
            #if excludeuuid == None:
            #    POOL.uuid = None
            #if excludeuuid == 'CRAP':
            #    POOL.uuid[4] = 244

            POOL.exclude(targets)

            if expected_result in ['FAIL']:
                   self.fail("Test was expected to fail but it passed.\n")

        except ValueError as e:
            print e
            print traceback.format_exc()
            if expected_result in ['PASS']:
                   self.fail("Test was expected to pass but it failed.\n")


if __name__ == "__main__":
    main()
