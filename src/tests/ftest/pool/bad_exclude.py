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

from apricot import TestWithServers
from pydaos.raw import DaosApiError


class BadExcludeTest(TestWithServers):
    """
    Tests target exclude calls passing NULL and otherwise inappropriate
    parameters.  This use the python API.

    :avocado: recursive
    """

    def test_exclude(self):
        """
        Pass bad parameters to pool connect

        :avocado: tags=all,pool,full_regression,tiny,badexclude
        """
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

        saved_grp = None
        saved_uuid = None
        self.prepare_pool()

        saved_grp = self.pool.pool.group
        if connectset == 'NULLPTR':
            # trash the pool group value
            self.pool.pool.group = None
        else:
            self.pool.pool.set_group(connectset)

        # trash the UUID value in various ways
        if excludeuuid == 'NULLPTR':
            self.cancel("skipping this test until DAOS-1932 is fixed")
            ctypes.memmove(saved_uuid, self.pool.pool.uuid, 16)
            self.pool.pool.uuid = 0
        if excludeuuid == 'CRAP':
            self.cancel("skipping this test until DAOS-1932 is fixed")
            ctypes.memmove(saved_uuid, self.pool.pool.uuid, 16)
            self.pool.pool.uuid[4] = 244

        try:
            self.pool.pool.exclude(targets)

            if expected_result == 'FAIL':
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
        finally:
            if saved_grp is not None:
                self.pool.pool.group = saved_grp
            if saved_uuid is not None:
                ctypes.memmove(self.pool.pool.uuid, saved_uuid, 16)
