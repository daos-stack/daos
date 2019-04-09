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
import time
import traceback
import sys
import json
import ctypes
from avocado import Test, main

sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./util')
sys.path.append('./../../utils/py')

import AgentUtils
import ServerUtils
import WriteHostFile
from daos_api import DaosContext, DaosPool, DaosApiError
from daos_cref import RankList

class BadConnectTest(Test):
    """
    Tests pool connect calls passing NULL and otherwise inappropriate
    parameters.  This can't be done with daosctl, need to use the python API.

    """

    # start servers, establish file locations, etc.
    def setUp(self):
        self.agent_sessions = None
        self.hostlist = None

        # get paths from the build_vars generated by build
        with open(os.path.join(os.path.dirname(os.path.realpath(__file__)),
                               "../../../../.build_vars.json")) as f:
            build_paths = json.load(f)
        self.basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
        tmp = build_paths['PREFIX'] + '/tmp'

        self.hostlist = self.params.get("test_machines",'/run/hosts/')

        # NULL is causing connect to blow up so skip that test for now
        uuidlist = self.params.get("uuid",'/run/connecttests/UUID/*/')
        connectuuid = uuidlist[0]
        if connectuuid == 'NULLPTR':
            self.cancel("skipping null pointer test until DAOS-1781 is fixed")

        # launch the server
        self.hostfile = WriteHostFile.WriteHostFile(self.hostlist, tmp)
        server_group = self.params.get("name", '/server_config/',
                                       'daos_server')

        self.agent_sessions = AgentUtils.run_agent(self.basepath, self.hostlist)

        ServerUtils.runServer(self.hostfile, server_group, self.basepath)

    def tearDown(self):
        if self.agent_sessions:
            AgentUtils.stop_agent(self.hostlist, self.agent_sessions)
        ServerUtils.stopServer(hosts=self.hostlist)

    def test_connect(self):
        """
        Pass bad parameters to pool connect

        :avocado: tags=pool,poolconnect,badparam,badconnect
        """

        # parameters used in pool create
        createmode = self.params.get("mode",'/run/connecttests/createmode/')
        createuid  = self.params.get("uid",'/run/connecttests/uids/createuid/')
        creategid  = self.params.get("gid",'/run/connecttests/gids/creategid/')
        createsetid = self.params.get("setname",
                                      '/run/connecttests/setnames/createset/')
        createsize  = self.params.get("size",
                                      '/run/connecttests/psize/createsize/')

        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode",'/run/connecttests/connectmode/*/')
        connectmode = modelist[0]
        expected_for_param.append(modelist[1])

        svclist = self.params.get("ranklist",'/run/connecttests/svrlist/*/')
        svc = svclist[0]
        expected_for_param.append(svclist[1])

        setlist = self.params.get("setname",
                                  '/run/connecttests/connectsetnames/*/')
        connectset = setlist[0]
        expected_for_param.append(setlist[1])

        uuidlist = self.params.get("uuid",'/run/connecttests/UUID/*/')
        connectuuid = uuidlist[0]
        expected_for_param.append(uuidlist[1])

        # if any parameter is FAIL then the test should FAIL, in this test
        # virtually everyone should FAIL since we are testing bad parameters
        expected_result = 'PASS'
        for result in expected_for_param:
                if result == 'FAIL':
                      expected_result = 'FAIL'
                      break

        puuid = (ctypes.c_ubyte * 16)()
        psvc = RankList()
        pgroup = ctypes.create_string_buffer(0)
        pool = None
        try:
            # setup the DAOS python API
            with open('../../../.build_vars.json') as f:
                data = json.load(f)
            CONTEXT = DaosContext(data['PREFIX'] + '/lib/')

            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(CONTEXT)
            pool.create(createmode, createuid, creategid,
                        createsize, createsetid, None)
            # save this uuid since we might trash it as part of the test
            ctypes.memmove(puuid,pool.uuid,16)

            # trash the the pool service rank list
            psvc.rl_ranks = pool.svc.rl_ranks
            psvc.rl_nr = pool.svc.rl_nr
            if not svc == 'VALID':
                rl_ranks = ctypes.POINTER(ctypes.c_uint)()
                pool.svc = RankList(rl_ranks, 1);

            # trash the pool group value
            pgroup = pool.group
            if connectset == 'NULLPTR':
                pool.group = None

            # trash the UUID value in various ways
            if connectuuid == 'NULLPTR':
                pool.uuid = None
            if connectuuid == 'JUNK':
                pool.uuid[4] = 244

            pool.connect(connectmode)

            if expected_result in ['FAIL']:
                    self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as e:
            print(e)
            print(traceback.format_exc())
            if expected_result in ['PASS']:
                    self.fail("Test was expected to pass but it failed.\n")

        # cleanup the pool
        finally:
            if pool is not None and pool.attached == 1:
                    # restore values in case we trashed them during test
                    pool.svc.rl_ranks = psvc.rl_ranks
                    pool.svc.rl_nr = psvc.rl_nr
                    pool.group = pgroup
                    ctypes.memmove(pool.uuid, puuid, 16)
                    print("pool uuid after restore {}".format(
                        pool.get_uuid_str()))
                    pool.destroy(1)