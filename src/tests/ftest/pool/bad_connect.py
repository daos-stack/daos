#!/usr/bin/python
'''
  (C) Copyright 2018-2020 Intel Corporation.

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

import traceback
import ctypes
from pydaos.raw import RankList
from avocado.core.exceptions import TestFail
from apricot import TestWithServers
from test_utils_pool import TestPool

class BadConnectTest(TestWithServers):
    """
    Tests pool connect calls passing NULL and otherwise inappropriate
    parameters.  This use the python API.
    :avocado: recursive
    """
    def test_connect(self):
        """
        Pass bad parameters to pool connect

        :avocado: tags=all,pool,full_regression,tiny,badconnect
        """
        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode", '/run/connecttests/connectmode/*/')
        connectmode = modelist[0]
        expected_for_param.append(modelist[1])

        svclist = self.params.get("ranklist", '/run/connecttests/svrlist/*/')
        svc = svclist[0]
        expected_for_param.append(svclist[1])

        setlist = self.params.get("setname",
                                  '/run/connecttests/connectsetnames/*/')
        connectset = setlist[0]
        expected_for_param.append(setlist[1])

        uuidlist = self.params.get("uuid", '/run/connecttests/UUID/*/')
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
        # initialize a python pool object then create the underlying
        # daos storage
        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)
        self.pool.create()
        # save this uuid since we might trash it as part of the test
        ctypes.memmove(puuid, self.pool.pool.uuid, 16)

        # trash the the pool service rank list
        psvc.rl_ranks = self.pool.pool.svc.rl_ranks
        psvc.rl_nr = self.pool.pool.svc.rl_nr
        if not svc == 'VALID':
            rl_ranks = ctypes.POINTER(ctypes.c_uint)()
            self.pool.pool.svc = RankList(rl_ranks, 1)

        # trash the pool group value
        pgroup = self.pool.pool.group
        if connectset == 'NULLPTR':
            self.pool.pool.group = None

        # trash the UUID value in various ways
        if connectuuid == 'NULLPTR':
            self.pool.pool.uuid = None
        if connectuuid == 'JUNK':
            self.pool.pool.uuid[4] = 244

        try:
            self.pool.connect(1 << connectmode)

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except TestFail as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result in ['PASS']:
                self.fail("Test was expected to pass but it failed.\n")

        # cleanup the pool
        finally:
            if self.pool is not None and self.pool.pool.attached == 1:
                # restore values in case we trashed them during test
                self.pool.pool.svc.rl_ranks = psvc.rl_ranks
                self.pool.pool.svc.rl_nr = psvc.rl_nr
                self.pool.pool.group = pgroup
                if self.pool.pool.uuid is None:
                    self.pool.pool.uuid = (ctypes.c_ubyte * 16)()
                ctypes.memmove(self.pool.pool.uuid, puuid, 16)
                print("pool uuid after restore {}".format(
                    self.pool.pool.get_uuid_str()))
