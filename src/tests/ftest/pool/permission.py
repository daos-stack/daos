#!/usr/bin/python3
'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import TestWithServers
from pydaos.raw import DaosContainer, DaosApiError
from avocado.core.exceptions import TestFail

RESULT_PASS = "PASS"  # nosec
RESULT_FAIL = "FAIL"


class Permission(TestWithServers):
    """Test pool permissions.

    Test Class Description:
        Tests DAOS pool permissions while connect, whether
        modifying file with specific permissions work as expected.

    :avocado: recursive
    """

    def test_file_modification(self):
        """Test ID: DAOS-???.

        Test Description:
            Test whether file modification happens as expected under different
            permission levels.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=pool
        :avocado: tags=permission,file_modification,test_file_modification
        """
        # parameter used for pool connect
        permissions = self.params.get("perm", '/run/createtests/permissions/*')
        expected_result = self.params.get("exp_result", '/run/createtests/permissions/*')

        # initialize a python pool object then create the underlying
        # daos storage
        self.add_pool(create=False)
        self.test_log.debug("Pool initialization successful")
        self.pool.create()
        self.test_log.debug("Pool Creation successful")
        try:
            self.pool.connect(1 << permissions)
            self.test_log.debug("Pool Connect successful")
        except TestFail as excep:
            self.log.error(str(excep))
            if expected_result == RESULT_PASS:
                self.fail(
                    "#Test was expected to pass but it failed at pool.connect.\n")
        try:
            self.container = DaosContainer(self.context)
            self.test_log.debug("Container initialization successful")

            self.container.create(self.pool.pool.handle)
            self.test_log.debug("Container create successful")

            # now open it
            self.container.open()
            self.test_log.debug("Container open successful")

            thedata = b"a string that I want to stuff into an object"
            size = 45
            dkey = b"this is the dkey"
            akey = b"this is the akey"

            self.container.write_an_obj(thedata, size, dkey, akey)
            self.test_log.debug("Container write successful")
            if expected_result == RESULT_FAIL:
                self.fail(
                    "Test was expected to fail at container operations " +
                    "but it passed.\n")
            else:
                self.test_log.debug("Test Passed.")
        except DaosApiError as excep:
            self.log.error(str(excep))
            if expected_result == RESULT_PASS:
                self.fail(
                    "#Test was expected to pass but it failed at container operations.\n")
            else:
                self.test_log.debug("Test expected failed in container create, r/w. Test Passed.")
