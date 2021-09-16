#!/usr/bin/python3
'''
  (C) Copyright 2017-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import traceback

from apricot import TestWithServers
from avocado.core.exceptions import TestFail


class SimpleCreateDeleteTest(TestWithServers):
    """
    Tests DAOS pool creation, trying both valid and invalid parameters.

    :avocado: recursive
    """

    def test_create(self):
        """
        Test basic pool creation.

        :avocado: tags=all,full_regression
        :avocado: tags=tiny
        :avocado: tags=pool,smoke,simple_create
        """
        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        uidlist = self.params.get("uid", '/run/tests/uids/*', os.geteuid())
        if uidlist[0] == 'valid':
            uid = os.geteuid()
        else:
            uid = uidlist[0]
        expected_for_param.append(uidlist[1])

        gidlist = self.params.get("gid", '/run/tests/gids/*', os.getegid())
        if gidlist[0] == 'valid':
            gid = os.getegid()
        else:
            gid = gidlist[0]
        expected_for_param.append(gidlist[1])

        setidlist = self.params.get("setname", '/run/tests/setnames/*')
        expected_for_param.append(setidlist[1])

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        self.add_pool(create=False)
        self.pool.uid = uid
        self.pool.gid = gid

        try:
            self.pool.create()
            if expected_result == 'FAIL':
                self.fail("Test was expected to fail but it passed.\n")
        except TestFail as exc:
            print(exc)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
