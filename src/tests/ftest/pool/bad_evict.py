'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import ctypes

from apricot import TestWithServers


class PoolBadEvictTest(TestWithServers):
    """Test pool evict calls.

    Test Class Description:
        Tests pool evict calls passing NULL and otherwise inappropriate
        parameters.

    :avocado: recursive
    """

    def test_pool_bad_evict(self):
        """Test ID: DAOS-427.

        Test Description:
            Pass bad parameters to the pool evict clients call.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool
        :avocado: tags=PoolBadEvictTest,test_pool_bad_evict
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

        self.add_pool(connect=False)
        original_test_pool_uuid = self.pool.uuid

        # trash the UUID value in various ways
        if excludeuuid is None:
            saveduuid = (ctypes.c_ubyte * 16)(0)
            for index, _ in enumerate(saveduuid):
                saveduuid[index] = self.pool.pool.uuid[index]
            self.pool.pool.uuid[0:] = \
                [0 for item in range(0, len(self.pool.pool.uuid))]
        elif excludeuuid == 'JUNK':
            saveduuid = (ctypes.c_ubyte * 16)(0)
            for index, _ in enumerate(saveduuid):
                saveduuid[index] = self.pool.pool.uuid[index]
            if self.pool.pool.uuid[4] != 244:
                self.pool.pool.uuid[4] = 244
            else:
                self.pool.pool.uuid[4] = 255

        self.pool.uuid = self.pool.pool.get_uuid_str()

        try:
            # Make dmg call and poolevict() not fail.
            self.pool.dmg.exit_status_exception = False
            self.pool.use_label = False
            self.pool.evict()
        finally:
            self.pool.use_label = True
            self.pool.dmg.exit_status_exception = True

        exit_status = self.pool.dmg.result.exit_status
        if exit_status == 0 and expected_result in ['FAIL']:
            self.fail("Test was expected to fail but it passed.\n")
        elif exit_status != 0 and expected_result in ['PASS']:
            self.fail("Test was expected to pass but it failed.\n")

        # if the test trashed some pool parameter, put it back the way it was
        if saveduuid is not None:
            self.pool.pool.uuid = saveduuid
            self.pool.uuid = original_test_pool_uuid
