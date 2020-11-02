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

import traceback

from apricot import TestWithServers

import check_for_pool


# pylint: disable=fixme, broad-except
class ConnectTest(TestWithServers):
    """Tests DAOS pool creation, calling it repeatedly one after another.

    :avocado: recursive
    """

    def test_connect(self):
        """Test connecting to a pool.

        :avocado: tags=all,pool,smoke,pr,small,poolconnect
        """
        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        set_id_list = self.params.get("setname", '/run/tests/setnames/*')
        set_id = set_id_list[0]
        expected_for_param.append(set_id_list[1])

        # if any parameter results in failure then the test should FAIL
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        host1 = self.hostlist_servers[0]
        host2 = self.hostlist_servers[1]

        dmg = self.get_dmg_command()
        scm_size = self.params.get("scm_size", "/run/pool*")

        try:
            data = dmg.pool_create(scm_size=scm_size, group=set_id)
            if dmg.result.exit_status != 0:
                self.fail("    Unable to parse the Pool's UUID and SVC.")

            print("uuid is {0}\n".format(data["uuid"]))

            exists = check_for_pool.check_for_pool(host1, data["uuid"])
            if exists != 0:
                self.fail(
                    "Pool {0} not found on host {1}.\n".format(
                        data["uuid"], host1))
            exists = check_for_pool.check_for_pool(host2, data["uuid"])
            if exists != 0:
                self.fail(
                    "Pool {0} not found on host {1}.\n".format(
                        data["uuid"], host2))

            dmg.pool_query(data["uuid"])
            if dmg.result.exit_status != 0:
                self.fail("Could not connect to Pool {}\n".format(data["uuid"]))

            if expected_result == 'FAIL':
                self.fail("Expected to fail but passed.\n")

        except Exception as error:
            print(error)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Expecting to pass but test has failed.\n")
