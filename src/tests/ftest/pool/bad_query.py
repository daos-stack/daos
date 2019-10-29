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

from apricot import TestWithServers
from pydaos.raw import DaosContext, DaosPool, DaosApiError


class BadQueryTest(TestWithServers):
    """Test pool query calls.

    Test Class Description:
        Tests pool query calls passing NULL and otherwise inappropriate
        parameters.  This can't be done with daosctl, need to use the python
        API.

    :avocado: recursive
    """

    def test_query(self):
        """Test ID: DAOS-???.

        Test Description:
            Pass bad parameters to pool query

        :avocado: tags=all,pool,full_regression,tiny,badquery
        """
        # parameters used in pool create/connect
        connectmode = self.params.get("mode", '/run/querytests/connectmode/')
        createmode = self.params.get("mode", '/run/querytests/createmode/')
        createuid = os.geteuid()
        creategid = os.getegid()
        createsetid = self.params.get("setname", '/run/querytests/createset/')
        createsize = self.params.get("size", '/run/querytests/createsize/')

        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        handlelist = self.params.get("handle", '/run/querytests/handles/*/')
        handle = handlelist[0]
        expected_for_param.append(handlelist[1])

        infolist = self.params.get("info", '/run/querytests/infoptr/*/')
        dummy_infoptr = infolist[0]
        expected_for_param.append(infolist[1])

        # if any parameter is FAIL then the test should FAIL, in this test
        # virtually everyone should FAIL since we are testing bad parameters
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            pool = DaosPool(self.context)
            pool.create(createmode, createuid, creategid,
                        createsize, createsetid, None)

            pool.connect(connectmode)

            # trash the pool handle value
            if not handle == 'VALID':
                pool.handle = handle

            pool.pool_query()

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except DaosApiError as excep:
            self.log.error(str(excep))
            self.log.error(traceback.format_exc())
            if expected_result in ['PASS']:
                self.fail("Test was expected to pass but it failed.\n")
