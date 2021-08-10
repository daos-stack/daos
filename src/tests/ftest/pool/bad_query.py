#!/usr/bin/python3
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback

from apricot import TestWithServers
from avocado.core.exceptions import TestFail


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

        :avocado: tags=all,full_regression
        :avocado: tags=pool
        :avocado: tags=tiny,bad_query
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
        self.add_pool()

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
