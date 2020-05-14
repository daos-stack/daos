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
from __future__ import print_function

import os
import traceback

from apricot import TestWithServers

import check_for_pool
import dmg_utils

# pylint: disable=fixme, broad-except
class ConnectTest(TestWithServers):
    """
    Tests DAOS pool creation, calling it repeatedly one after another

    :avocado: recursive
    """

    def test_connect(self):
        """
        Test connecting to a pool.

        :avocado: tags=all,pool,smoke,pr,small,poolconnect
        """

        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        setidlist = self.params.get("setname", '/run/tests/setnames/*')
        setid = setidlist[0]
        expected_for_param.append(setidlist[1])

        # if any parameter results in failure then the test should FAIL
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            uid = os.geteuid()
            gid = os.getegid()

            host1 = self.hostlist_servers[0]
            host2 = self.hostlist_servers[1]

            dmg = self.get_dmg_command()
            scm_size = self.params.get("scm_size", "/run/pool*")
            result = dmg.pool_create(scm_size=scm_size, uid=uid, gid=gid,
                                     group=setid, svcn=1)
            if "ERR" not in result.stderr:
                uuid_str, _ = \
                    dmg_utils.get_pool_uuid_service_replicas_from_stdout(
                        result.stdout)
            else:
                self.fail("    Unable to parse the Pool's UUID and SVC.")

            print("uuid is {0}\n".format(uuid_str))

            exists = check_for_pool.check_for_pool(host1, uuid_str)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n".
                          format(uuid_str, host1))
            exists = check_for_pool.check_for_pool(host2, uuid_str)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n".
                          format(uuid_str, host2))

            result = dmg.pool_query(uuid_str)


            if expected_result == 'FAIL':
                self.fail("Expected to fail but passed.\n")

        except Exception as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Expecting to pass but test has failed.\n")
