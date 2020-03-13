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
from apricot import TestWithServers
from daos.raw import DaosContainer, DaosApiError
from avocado.core.exceptions import TestFail
from test_utils_pool import TestPool

RESULT_PASS = "PASS"
RESULT_FAIL = "FAIL"


class Permission(TestWithServers):
    """Test pool permissions.

    Test Class Description:
        Tests DAOS pool permissions while connect, whether
        modifying file with specific permissions work as expected.

    :avocado: recursive
    """

    def test_connectpermission(self):
        """Test ID: DAOS-???.

        Test Description:
            Test pool connections with specific permissions.

        :avocado: tags=pool,permission,connectpermission
        """
        # parameter used in pool create
        createmode = self.params.get("mode", '/run/createtests/createmode/*/')

        # parameter used for pool connect
        permissions = self.params.get("perm", '/run/createtests/permissions/*')

        if createmode == 73 or \
           (createmode == 146 and permissions != 1) or \
           (createmode == 292 and permissions != 3):
            self.cancelForTicket("DAOS-3442")

        if createmode == 73:
            expected_result = RESULT_FAIL
        if createmode == 511 and permissions == 0:
            expected_result = RESULT_PASS
        elif createmode in [146, 511] and permissions == 1:
            expected_result = RESULT_PASS
        elif createmode in [292, 511] and permissions == 2:
            expected_result = RESULT_PASS
        else:
            expected_result = RESULT_FAIL

        # initialize a python pool object then create the underlying
        # daos storage
        self.pool = TestPool(self.context, dmg_command=self.get_dmg_command())
        self.multi_log("Pool initialisation successful", "debug")
        self.pool.get_params(self)
        self.pool.mode.value = createmode
        self.pool.create()
        self.multi_log("Pool Creation successful", "debug")

        try:
            self.pool.connect(1 << permissions)
            self.multi_log("Pool Connect successful", "debug")

            if expected_result == RESULT_FAIL:
                self.fail(
                    "Test was expected to fail at pool.connect, but it " +
                    "passed.\n")

        except TestFail as excep:
            self.log.error(str(excep))
            if expected_result == RESULT_PASS:
                self.fail(
                    "Test was expected to pass but it failed at " +
                    "pool.connect.\n")

    def test_filemodification(self):
        """Test ID: DAOS-???.

        Test Description:
            Test whether file modification happens as expected under different
            permission levels.

        :avocado: tags=pool,permission,filemodification
        """
        # parameters used in pool create
        createmode = self.params.get("mode", '/run/createtests/createmode/*/')

        if createmode == 73:
            expected_result = RESULT_FAIL
        elif createmode in [146, 511]:
            permissions = 1
            expected_result = RESULT_PASS
        elif createmode == 292:
            permissions = 2
            expected_result = RESULT_PASS

        # initialize a python pool object then create the underlying
        # daos storage
        self.pool = TestPool(self.context, dmg_command=self.get_dmg_command())
        self.multi_log("Pool initialisation successful", "debug")
        self.pool.get_params(self)
        self.pool.mode.value = createmode
        self.multi_log("Pool Creation successful", "debug")

        try:
            self.pool.connect(1 << permissions)
            self.multi_log("Pool Connect successful", "debug")
            if expected_result == RESULT_FAIL:
                self.fail(
                    "Test was expected to fail at pool.connect but it " +
                    "passed.\n")
        except TestFail as excep:
            self.log.error(str(excep))
            if expected_result == RESULT_PASS:
                self.fail(
                    "Test was expected to pass but it failed at " +
                    "pool.connect.\n")

        try:
            self.container = DaosContainer(self.context)
            self.multi_log("Contianer initialisation successful", "debug")

            self.container.create(self.pool.pool.handle)
            self.multi_log("Container create successful", "debug")

            # now open it
            self.container.open()
            self.multi_log("Container open successful", "debug")

            thedata = "a string that I want to stuff into an object"
            size = 45
            dkey = "this is the dkey"
            akey = "this is the akey"

            self.container.write_an_obj(thedata, size, dkey, akey)
            self.multi_log("Container write successful", "debug")
            if expected_result == RESULT_FAIL:
                self.fail(
                    "Test was expected to fail at container operations " +
                    "but it passed.\n")

        except DaosApiError as excep:
            self.log.error(str(excep))
            if expected_result == RESULT_PASS:
                self.fail(
                    "Test was expected to pass but it failed at container " +
                    "operations.\n")
