#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

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

# pylint: disable = broad-except
class MultipleCreatesTest(TestWithServers):
    """
    Tests DAOS pool creation, calling it repeatedly one after another

    :avocado: recursive
    """

    def test_create_one(self):
        """
        Test issuing a single  pool create commands at once.

        :avocado: tags=all,pool,smoke,pr,small,createone
        """

        # Accumulate a list of pass/fail indicators representing
        # what is expected for each parameter then "and" them
        # to determine the expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode", '/run/tests/modes/*')
        expected_for_param.append(modelist[1])

        setidlist = self.params.get("setname", '/run/tests/setnames/*')
        setid = setidlist[0]
        expected_for_param.append(setidlist[1])

        scm_size = self.params.get("scm_size", "/run/pool*")

        uid = os.geteuid()
        gid = os.getegid()

        # if any parameter results in failure then the test should FAIL
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            dmg = self.get_dmg_command()
            result = dmg.pool_create(scm_size=scm_size, uid=uid, gid=gid, group=setid, svcn=1)
            if "ERR" not in result.stderr:
                uuid_str, _ = \
                    dmg_utils.get_pool_uuid_service_replicas_from_stdout(
                        result.stdout)
            else:
                self.fail("    Unable to parse the Pool's UUID and SVC.")

            print("uuid is {0}\n".format(uuid_str))

            host = self.hostlist_servers[0]
            exists = check_for_pool.check_for_pool(host, uuid_str)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n".format(uuid_str,
                                                                     host))

            result = dmg.pool_destroy(pool=uuid_str)
            if "failed" in result.stdout:
                self.log.info("Unable to destroy pool %s", uuid_str)

            exists = check_for_pool.check_for_pool(host, uuid_str)
            if exists == 0:
                self.fail("Pool {0} found on host {1} after destroy.\n"
                          .format(uuid_str, host))

            if expected_result == 'FAIL':
                self.fail("Expected to fail but passed.\n")

        except Exception as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Expecting to pass but test has failed.\n")


    def test_create_two(self):
        # pylint: disable=too-many-statements
        """
        Test issuing multiple pool create commands at once.

        :avocado: tags=all,pool,smoke,pr,small,createtwo
        """

        # Accumulate a list of pass/fail indicators representing
        # what is expected for each parameter then "and" them to
        # determine the expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode", '/run/tests/modes/*')
        expected_for_param.append(modelist[1])

        setidlist = self.params.get("setname", '/run/tests/setnames/*')
        setid = setidlist[0]
        expected_for_param.append(setidlist[1])

        scm_size = self.params.get("scm_size", "/run/pool*")

        uid = os.geteuid()
        gid = os.getegid()

        # if any parameter results in failure then the test should FAIL
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            dmg = self.get_dmg_command()
            result = dmg.pool_create(scm_size=scm_size, uid=uid, gid=gid, group=setid, svcn=1)
            if "ERR" not in result.stderr:
                uuid_str_1, _ = \
                    dmg_utils.get_pool_uuid_service_replicas_from_stdout(
                        result.stdout)
            else:
                self.fail("    Unable to parse the Pool's UUID and SVC.")

            result = dmg.pool_create(scm_size=scm_size, uid=uid, gid=gid, group=setid, svcn=1)
            if "ERR" not in result.stderr:
                uuid_str_2, _ = \
                    dmg_utils.get_pool_uuid_service_replicas_from_stdout(
                        result.stdout)
            else:
                self.fail("    Unable to parse the Pool's UUID and SVC.")

            host = self.hostlist_servers[0]
            exists = check_for_pool.check_for_pool(host, uuid_str_1)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_1,
                                                                     host))
            exists = check_for_pool.check_for_pool(host, uuid_str_2)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_2,
                                                                     host))

            result = dmg.pool_destroy(pool=uuid_str_1)
            if "failed" in result.stdout:
                self.log.info("Unable to destroy pool %s", uuid_str_1)

            result = dmg.pool_destroy(pool=uuid_str_2)
            if "failed" in result.stdout:
                self.log.info("Unable to destroy pool %s", uuid_str_2)


            exists = check_for_pool.check_for_pool(host, uuid_str_1)
            if exists == 0:
                self.fail("Pool {0} found on host {1} after destroy.\n"
                          .format(uuid_str_1, host))
            exists = check_for_pool.check_for_pool(host, uuid_str_2)
            if exists == 0:
                self.fail("Pool {0} found on host {1} after destroy.\n"
                          .format(uuid_str_2, host))

            if expected_result == 'FAIL':
                self.fail("Expected to fail but passed.\n")

        except Exception as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Expecting to pass but test has failed.\n")


    def test_create_three(self):
        # pylint: disable=too-many-statements
        """
        Test issuing multiple pool create commands at once.

        :avocado: tags=all,pool,pr,small,createthree
        """

        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode", '/run/tests/modes/*')
        expected_for_param.append(modelist[1])

        setidlist = self.params.get("setname", '/run/tests/setnames/*')
        setid = setidlist[0]
        expected_for_param.append(setidlist[1])

        scm_size = self.params.get("scm_size", "/run/pool*")

        uid = os.geteuid()
        gid = os.getegid()

        # if any parameter results in failure then the test should FAIL
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            dmg = self.get_dmg_command()
            result = dmg.pool_create(scm_size=scm_size, uid=uid, gid=gid, group=setid, svcn=1)
            if "ERR" not in result.stderr:
                uuid_str_1, _ = \
                    dmg_utils.get_pool_uuid_service_replicas_from_stdout(
                        result.stdout)
            else:
                self.fail("    Unable to parse the Pool's UUID and SVC.")

            result = dmg.pool_create(scm_size=scm_size, uid=uid, gid=gid, group=setid, svcn=1)
            if "ERR" not in result.stderr:
                uuid_str_2, _ = \
                    dmg_utils.get_pool_uuid_service_replicas_from_stdout(
                        result.stdout)
            else:
                self.fail("    Unable to parse the Pool's UUID and SVC.")

            result = dmg.pool_create(scm_size=scm_size, uid=uid, gid=gid, group=setid, svcn=1)
            if "ERR" not in result.stderr:
                uuid_str_3, _ = \
                    dmg_utils.get_pool_uuid_service_replicas_from_stdout(
                        result.stdout)
            else:
                self.fail("    Unable to parse the Pool's UUID and SVC.")


            host = self.hostlist_servers[0]
            exists = check_for_pool.check_for_pool(host, uuid_str_1)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_1,
                                                                     host))
            exists = check_for_pool.check_for_pool(host, uuid_str_2)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_2,
                                                                     host))
            exists = check_for_pool.check_for_pool(host, uuid_str_3)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_3,
                                                                     host))

            result = dmg.pool_destroy(pool=uuid_str_1)
            if "failed" in result.stdout:
                self.log.info("Unable to destroy pool %s", uuid_str_1)

            result = dmg.pool_destroy(pool=uuid_str_2)
            if "failed" in result.stdout:
                self.log.info("Unable to destroy pool %s", uuid_str_2)

            result = dmg.pool_destroy(pool=uuid_str_3)
            if "failed" in result.stdout:
                self.log.info("Unable to destroy pool %s", uuid_str_3)


            exists = check_for_pool.check_for_pool(host, uuid_str_1)
            if exists == 0:
                self.fail("Pool {0} found on host {1} after destroy.\n"
                          .format(uuid_str_1, host))
            exists = check_for_pool.check_for_pool(host, uuid_str_2)
            if exists == 0:
                self.fail("Pool {0} found on host {1} after destroy.\n"
                          .format(uuid_str_2, host))
            exists = check_for_pool.check_for_pool(host, uuid_str_3)
            if exists == 0:
                self.fail("Pool {0} found on host {1} after destroy.\n"
                          .format(uuid_str_3, host))

            if expected_result == 'FAIL':
                self.fail("Expected to fail but passed.\n")

        except Exception as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Expecting to pass but test has failed.\n")

    # COMMENTED OUT because test environments don't always have enough
    # memory to run this
    #def test_create_five(self):
    #    """
    #    Test issuing five pool create commands at once.
    #    """
    #
    # Accumulate a list of pass/fail indicators representing what is
    # expected for each parameter then "and" them to determine the
    # expected result of the test
    #    expected_for_param = []
    #
    #    modelist = self.params.get("mode",'/run/tests/modes/*')
    #    mode = modelist[0]
    #    expected_for_param.append(modelist[1])
    #
    #    setidlist = self.params.get("setname",'/run/tests/setnames/*')
    #    setid = setidlist[0]
    #    expected_for_param.append(setidlist[1])
    #
    #    uid = os.geteuid()
    #    gid = os.getegid()
    #
    #    # if any parameter results in failure then the test should FAIL
    #    expected_result = 'PASS'
    #    for result in expected_for_param:
    #           if result == 'FAIL':
    #                  expected_result = 'FAIL'
    #                  break
    #    try:
    #           cmd = ('../../install/bin/orterun -np 1 '
    #                  '--ompi-server file:{0} ./pool/wrap'
    #                  'per/SimplePoolTests {1} {2} {3} {4} {5}'.format(
    #                      urifile, "create", mode, uid, gid, setid))
    #           process.system(cmd)
    #           process.system(cmd)
    #           process.system(cmd)
    #           process.system(cmd)
    #           process.system(cmd)
    #
    #           if expected_result == 'FAIL':
    #                  self.fail("Expected to fail but passed.\n")
    #
    #    except Exception as e:
    #           print(e)
    #           print(traceback.format_exc())
    #           if expected_result == 'PASS':
    #                  self.fail("Expecting to pass but test has failed.\n")
