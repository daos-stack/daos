#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback
import ctypes

from apricot import TestWithServers
from pydaos.raw import DaosApiError
from test_utils_pool import TestPool


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
        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

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
        pool = None

        try:
            # initialize a python pool object then create the underlying
            # daos storage
            pool = TestPool(self.context, self.get_dmg_command())
            pool.get_params(self)
            pool.create()

            # trash the UUID value in various ways
            if excludeuuid is None:
                saveduuid = (ctypes.c_ubyte * 16)(0)
                for item in range(0, len(saveduuid)):
                    saveduuid[item] = pool.pool.uuid[item]
                pool.pool.uuid[0:] = \
                    [0 for item in range(0, len(pool.pool.uuid))]
            elif excludeuuid == 'JUNK':
                saveduuid = (ctypes.c_ubyte * 16)(0)
                for item in range(0, len(saveduuid)):
                    saveduuid[item] = pool.pool.uuid[item]
                pool.pool.uuid[4] = 244

            # evict the pool
            pool.pool.evict()

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
                if saveduuid is not None:
                    pool.pool.uuid = saveduuid
                pool.destroy()
