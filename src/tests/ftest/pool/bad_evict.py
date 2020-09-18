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
import traceback
import ctypes

from apricot import TestWithServers
from pydaos.raw import DaosPool, DaosApiError, RankList


class BadEvictTest(TestWithServers):
    """Test pool evict calls.

    Test Class Description:
        Tests pool evict calls passing NULL and otherwise inappropriate
        parameters.

    :avocado: recursive
    """

    def test_evict(self):
        """Test ID: DAOS-427.

        Test Description:
            Pass bad parameters to the pool evict clients call.

        :avocado: tags=all,pool,full_regression,tiny,badevict
        """
        # parameters used in pool create
        createmode = self.params.get("mode", '/run/evicttests/createmode/')
        createsetid = self.params.get("setname", '/run/evicttests/createset/')
        createsize = self.params.get("size", '/run/evicttests/createsize/')

        createuid = os.geteuid()
        creategid = os.getegid()

        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        svclist = self.params.get("ranklist", '/run/evicttests/svrlist/*/')
        svc = svclist[0]
        expected_for_param.append(svclist[1])

        setlist = self.params.get("setname",
                                  '/run/evicttests/connectsetnames/*/')
        evictset = setlist[0]
        expected_for_param.append(setlist[1])

        uuidlist = self.params.get("uuid", '/run/evicttests/UUID/*/')
        excludeuuid = uuidlist[0]
        expected_for_param.append(uuidlist[1])

        # if any parameter is FAIL then the test should FAIL, in this test
        # virtually everyone should FAIL since we are testing bad parameters
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        saveduuid = None
        savedgroup = None
        savedsvc = None
        pool = None

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(self.context)
            pool.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            # trash the the pool service rank list
            if not svc == 'VALID':
                savedsvc = pool.svc
                rl_ranks = ctypes.POINTER(ctypes.c_uint)()
                pool.svc = RankList(rl_ranks, 1)

            # trash the pool group value
            savedgroup = pool.group
            if evictset is None:
                pool.group = None
            else:
                pool.set_group(evictset)

            # trash the UUID value in various ways
            if excludeuuid is None:
                saveduuid = (ctypes.c_ubyte * 16)(0)
                for item in range(0, len(saveduuid)):
                    saveduuid[item] = pool.uuid[item]
                pool.uuid[0:] = [0 for item in range(0, len(pool.uuid))]
            elif excludeuuid == 'JUNK':
                saveduuid = (ctypes.c_ubyte * 16)(0)
                for item in range(0, len(saveduuid)):
                    saveduuid[item] = pool.uuid[item]
                pool.uuid[4] = 244

            pool.evict()

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            self.log.error(str(excep))
            self.log.error(traceback.format_exc())
            if expected_result in ['PASS']:
                self.fail("Test was expected to pass but it failed.\n")
        finally:
            if pool is not None:
                # if the test trashed some pool parameter, put it back the
                # way it was
                pool.group = savedgroup
                if saveduuid is not None:
                    for item in range(0, len(saveduuid)):
                        pool.uuid[item] = saveduuid[item]
                if savedsvc is not None:
                    pool.svc = savedsvc
                pool.destroy(0)
