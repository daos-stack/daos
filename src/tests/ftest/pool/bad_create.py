#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import traceback

from apricot import TestWithServers
from test_utils_pool import TestPool
from avocado.core.exceptions import TestFail


class BadCreateTest(TestWithServers):
    """Test pool create calls.

    Test Class Description:
        Tests pool create API by passing NULL and otherwise inappropriate
        parameters.  This use the python API.

    :avocado: recursive
    """

    def test_create(self):
        """Test ID: DAOS-???.

        Test Description:
            Pass bad parameters to pool create.

        :avocado: tags=all,pool,full_regression,tiny,badcreate
        """
        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode", '/run/createtests/modes/*')
        mode = modelist[0]
        expected_for_param.append(modelist[1])

        uidlist = self.params.get("uid", '/run/createtests/uids/*')
        uid = uidlist[0]
        if uid == 'VALID':
            uid = os.geteuid()
        expected_for_param.append(uidlist[1])

        gidlist = self.params.get("gid", '/run/createtests/gids/*')
        gid = gidlist[0]
        if gid == 'VALID':
            gid = os.getegid()
        expected_for_param.append(gidlist[1])

        setidlist = self.params.get("setname", '/run/createtests/setnames/*')
        if setidlist[0] == 'NULLPTR':
            group = None
            self.cancel("skipping this test until DAOS-1991 is fixed")
        else:
            group = setidlist[0]
        expected_for_param.append(setidlist[1])

        # Uncomment this block when we test targetptr.
        # targetlist = self.params.get("rankptr", '/run/createtests/target/*')
        # if targetlist[0] == 'NULL':
        #     targetptr = None
        # else:
        #     targetptr = [0]
        # expected_for_param.append(targetlist[1])

        # not ready for this yet
        # devicelist = self.params.get("devptr", '/run/createtests/device/*')
        # if devicelist[0] == 'NULL':
        #    devptr = None
        # else:
        #    devptr = devicelist[0]
        # expected_for_param.append(devicelist[1])

        sizelist = self.params.get("size", '/run/createtests/psize/*')
        size = sizelist[0]
        expected_for_param.append(sizelist[1])

        # parameter not presently supported
        # svclist = self.params.get("rankptr", '/run/createtests/svc/*')
        # if svclist[0] == 'NULL':
        #    svc = None
        # else:
        #    svc = None
        # expected_for_param.append(devicelist[1])

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
        # Manually set TestPool members before calling create
        self.pool.mode.value = mode
        self.pool.uid = uid
        self.pool.gid = gid
        self.pool.scm_size.value = size
        self.pool.name.value = group

        try:
            self.pool.create()

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        # create with invalid parameter will cause test failure, so catch it if
        # we expect it
        except TestFail as excep:
            self.log.error(str(excep))
            self.log.error(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
