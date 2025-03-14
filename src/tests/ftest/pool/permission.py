'''
  (C) Copyright 2018-2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from pydaos.raw import DaosApiError
from test_utils_container import add_container
from test_utils_pool import add_pool

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
        :avocado: tags=Permission,test_file_modification
        """
        # parameter used for pool connect
        permissions = self.params.get("perm", '/run/createtests/permissions/*')
        expected_result = self.params.get("exp_result", '/run/createtests/permissions/*')

        # initialize a python pool object then create the underlying
        # daos storage
        pool = add_pool(self, create=False)
        self.log.debug("Pool initialization successful")
        pool.create()
        self.log.debug("Pool Creation successful")
        try:
            pool.connect(1 << permissions)
            self.log.debug("Pool Connect successful")
        except TestFail as excep:
            self.log.error(str(excep))
            if expected_result == RESULT_PASS:
                self.fail("Test was expected to pass but it failed at pool.connect.")

        container = add_container(self, pool, create=False)
        self.log.debug("Container initialization successful")
        try:
            container.create()
            self.log.debug("Container create successful")
            # now open it
            container.open()
            self.log.debug("Container open successful")
        except TestFail as error:
            self.log.error(str(error))
            if expected_result == RESULT_PASS:
                self.fail("Test was expected to pass but it failed at container operations.")

        thedata = b"a string that I want to stuff into an object"
        size = 45
        dkey = b"this is the dkey"
        akey = b"this is the akey"
        try:
            container.container.write_an_obj(thedata, size, dkey, akey)
            self.log.debug("Container write successful")
            if expected_result == RESULT_FAIL:
                self.fail("Test was expected to fail at container operations but it passed.")
        except DaosApiError as error:
            self.log.error(str(error))
            if expected_result == RESULT_PASS:
                self.fail("Test was expected to pass but it failed at container operations.")
            else:
                self.log.debug("Test expected failed in container create, r/w.")
        self.log.debug("Test Passed.")
