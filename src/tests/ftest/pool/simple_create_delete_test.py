#!/usr/bin/python
'''
  (C) Copyright 2017-2020 Intel Corporation.

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
from __future__ import print_function

import os
import traceback

from apricot import TestWithServers
from test_utils_pool import TestPool
from avocado.core.exceptions import TestFail


class SimpleCreateDeleteTest(TestWithServers):
    """
    Tests DAOS pool creation, trying both valid and invalid parameters.

    :avocado: recursive
    """

    def test_create(self):
        """
        Test basic pool creation.

        :avocado: tags=all,pool,smoke,full_regression,tiny,simplecreate
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
        setid = setidlist[0]
        expected_for_param.append(setidlist[1])

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        try:
            self.pool = TestPool(self.context, self.get_dmg_command())
            self.pool.get_params(self)
            self.pool.uid = uid
            self.pool.gid = gid
            self.pool.name.update(setid)
            self.pool.create()
            if expected_result == 'FAIL':
                self.fail("Test was expected to fail but it passed.\n")

        except TestFail as exc:
            print(exc)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
