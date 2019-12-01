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
import ctypes
import agent_utils
import server_utils
import write_host_file
from apricot import TestWithoutServers
from pydaos.raw import DaosContext, DaosPool, DaosApiError, RankList

class BadExcludeTest(TestWithoutServers):
    """
    Tests target exclude calls passing NULL and otherwise inappropriate
    parameters.  This can't be done with daosctl, need to use the python API.

    :avocado: recursive
    """

    def setUp(self):
        super(BadExcludeTest, self).setUp()
        self.agent_sessions = None
        self.hostlist_servers = self.params.get("test_machines", '/run/hosts/')

        self.hostfile_servers = write_host_file.write_host_file(
            self.hostlist_servers, self.workdir)

        server_group = self.params.get("name",
                                       '/server_config/',
                                       'daos_server')
        self.agent_sessions = agent_utils.run_agent(self,
                                                    self.hostlist_servers)
        server_utils.run_server(self, self.hostfile_servers, server_group)

    def tearDown(self):
        if self.agent_sessions:
            agent_utils.stop_agent(self.agent_sessions)
        server_utils.stop_server(hosts=self.hostlist_servers)

    def test_exclude(self):
        """
        Pass bad parameters to pool connect

        :avocado: tags=all,pool,full_regression,tiny,badexclude
        """
        # parameters used in pool create
        createmode = self.params.get("mode", '/run/pool/createmode/')
        createsetid = self.params.get("setname", '/run/pool/createset/')
        createsize = self.params.get("size", '/run/pool/createsize/')

        createuid = os.geteuid()
        creategid = os.getegid()

        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        tgtlist = self.params.get("ranklist", '/run/testparams/tgtlist/*/')
        targets = []

        if tgtlist[0] == "NULLPTR":
            targets = None
            self.cancel("skipping null pointer test until DAOS-1929 is fixed")
        else:
            targets.append(tgtlist[0])
        expected_for_param.append(tgtlist[1])

        svclist = self.params.get("ranklist", '/run/testparams/svrlist/*/')
        svc = svclist[0]
        expected_for_param.append(svclist[1])

        setlist = self.params.get("setname",
                                  '/run/testparams/connectsetnames/*/')
        connectset = setlist[0]
        expected_for_param.append(setlist[1])

        uuidlist = self.params.get("uuid", '/run/testparams/UUID/*/')
        excludeuuid = uuidlist[0]
        expected_for_param.append(uuidlist[1])

        # if any parameter is FAIL then the test should FAIL, in this test
        # virtually everyone should FAIL since we are testing bad parameters
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        saved_svc = None
        saved_grp = None
        saved_uuid = None
        pool = None
        try:
            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(self.context)
            pool.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            # trash the the pool service rank list
            if not svc == 'VALID':
                self.cancel("skipping this test until DAOS-1931 is fixed")
                saved_svc = RankList(pool.svc.rl_ranks, pool.svc.rl_nr)
                pool.svc = None

            # trash the pool group value
            if connectset == 'NULLPTR':
                saved_grp = pool.group
                pool.group = None

            # trash the UUID value in various ways
            if excludeuuid == 'NULLPTR':
                self.cancel("skipping this test until DAOS-1932 is fixed")
                ctypes.memmove(saved_uuid, pool.uuid, 16)
                pool.uuid = 0
            if excludeuuid == 'CRAP':
                self.cancel("skipping this test until DAOS-1932 is fixed")
                ctypes.memmove(saved_uuid, pool.uuid, 16)
                pool.uuid[4] = 244

            pool.exclude(targets)

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result in ['PASS']:
                self.fail("Test was expected to pass but it failed.\n")
        finally:
            if pool is not None:
                if saved_svc is not None:
                    pool.svc = saved_svc
                if saved_grp is not None:
                    pool.group = saved_grp
                if saved_uuid is not None:
                    ctypes.memmove(pool.uuid, saved_uuid, 16)

                pool.destroy(1)
