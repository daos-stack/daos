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
import traceback

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from test_utils_pool import TestPool

class BadQueryTest(TestWithServers):
    """Test pool query calls.

    Test Class Description:
        Tests pool query calls passing NULL and otherwise inappropriate
        parameters.  This use the python API.

    :avocado: recursive
    """

    def test_query(self):
        """Test ID: DAOS-3821.

        Test Description:
            Pass bad parameters to pool query

        :avocado: tags=all,pool,full_regression,tiny,badquery
        """
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

        # initialize a python pool object then create the underlying
        # daos storage
        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)
        self.pool.create()
        self.pool.connect()

        # trash the pool handle value
        if not handle == 'VALID':
            handle_sav = self.pool.pool.handle
            self.pool.pool.handle = handle

        try:
            self.pool.get_info()

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")
            self.log.info("===Pool query positive testcase Passed.")

        except TestFail as excep:
            self.log.error(str(excep))
            self.log.error(traceback.format_exc())
            if expected_result in ['PASS']:
                self.fail("Test was expected to pass but it failed.\n")
            self.log.info("===Pool query negative testcase Passed.")
            # restore the valid handle for pool cleanup
            self.pool.pool.handle = handle_sav
