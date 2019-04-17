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
import json

from avocado.utils import process
from apricot import Test

import agent_utils
import server_utils
import check_for_pool
import write_host_file

class MultipleCreatesTest(Test):
    """
    Tests DAOS pool creation, calling it repeatedly one after another

    :avocado: recursive
    """

    # super wasteful since its doing this for every variation
    def setUp(self):

        # there is a presumption that this test lives in a specific
        # spot in the repo
        with open('../../../.build_vars.json') as build_file:
            build_paths = json.load(build_file)
        basepath = os.path.normpath(build_paths['PREFIX'] + "/../")

        self.hostlist_servers = self.params.get("test_machines", '/run/hosts/')
        self.hostfile_servers = write_host_file.write_host_file(
            self.hostlist_servers, self.workdir)

        server_group = self.params.get("server_group", '/server/',
                                       'daos_server')

        self.agent_sessions = agent_utils.run_agent(basepath,
                                                    self.hostlist_servers)
        server_utils.run_server(self.hostfile_servers, server_group, basepath)

        self.daosctl = basepath + '/install/bin/daosctl'


    def tearDown(self):
        try:
            os.remove(self.hostfile_servers)
        finally:
            if self.agent_sessions:
                agent_utils.stop_agent(self.hostlist_servers,
                                       self.agent_sessions)
            server_utils.stop_server(hosts=self.hostlist_servers)

    def test_create_one(self):
        """
        Test issuing a single  pool create commands at once.

        :avocado: tags=pool,poolcreate,multicreate
        """

        # Accumulate a list of pass/fail indicators representing
        # what is expected for each parameter then "and" them
        # to determine the expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode", '/run/tests/modes/*')
        mode = modelist[0]
        expected_for_param.append(modelist[1])

        setidlist = self.params.get("setname", '/run/tests/setnames/*')
        setid = setidlist[0]
        expected_for_param.append(setidlist[1])

        uid = os.geteuid()
        gid = os.getegid()

        # if any parameter results in failure then the test should FAIL
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            cmd = (
                "{0} create-pool "
                "-m {1} "
                "-u {2} "
                "-g {3} "
                "-s {4} "
                "-c 1".format(self.daosctl, mode, uid, gid, setid))

            uuid_str = """{0}""".format(process.system_output(cmd))
            print("uuid is {0}\n".format(uuid_str))

            host = self.hostlist_servers[0]
            exists = check_for_pool.check_for_pool(host, uuid_str)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n".format(uuid_str,
                                                                     host))

            delete_cmd = (
                "{0} destroy-pool "
                "-i {1} "
                "-s {2} "
                "-f".format(self.daosctl, uuid_str, setid))

            process.system(delete_cmd)

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
        """
        Test issuing multiple pool create commands at once.

        :avocado: tags=pool,poolcreate,multicreate
        """

        # Accumulate a list of pass/fail indicators representing
        # what is expected for each parameter then "and" them to
        # determine the expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode", '/run/tests/modes/*')
        mode = modelist[0]
        expected_for_param.append(modelist[1])

        setidlist = self.params.get("setname", '/run/tests/setnames/*')
        setid = setidlist[0]
        expected_for_param.append(setidlist[1])

        uid = os.geteuid()
        gid = os.getegid()

        # if any parameter results in failure then the test should FAIL
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            cmd = (
                "{0} create-pool "
                "-m {1} "
                "-u {2} "
                "-g {3} "
                "-s {4} "
                "-c 1".format(self.daosctl, mode, uid, gid, setid))

            uuid_str_1 = """{0}""".format(process.system_output(cmd))
            uuid_str_2 = """{0}""".format(process.system_output(cmd))

            host = self.hostlist_servers[0]
            exists = check_for_pool.check_for_pool(host, uuid_str_1)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_1,
                                                                     host))
            exists = check_for_pool.check_for_pool(host, uuid_str_2)
            if exists != 0:
                self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_2,
                                                                     host))

            delete_cmd_1 = (
                "{0} destroy-pool "
                "-i {1} "
                "-s {2} "
                "-f".format(self.daosctl, uuid_str_1, setid))

            delete_cmd_2 = (
                "{0} destroy-pool "
                "-i {1} "
                "-s {2} "
                "-f".format(self.daosctl, uuid_str_2, setid))

            process.system(delete_cmd_1)
            process.system(delete_cmd_2)

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
        """
        Test issuing multiple pool create commands at once.

        :avocado: tags=pool,poolcreate,multicreate
        """

        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode", '/run/tests/modes/*')
        mode = modelist[0]
        expected_for_param.append(modelist[1])

        setidlist = self.params.get("setname", '/run/tests/setnames/*')
        setid = setidlist[0]
        expected_for_param.append(setidlist[1])

        uid = os.geteuid()
        gid = os.getegid()

        # if any parameter results in failure then the test should FAIL
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break
        try:
            cmd = (
                "{0} create-pool "
                "-m {1} "
                "-u {2} "
                "-g {3} "
                "-s {4} "
                "-c 1".format(self.daosctl, mode, uid, gid, setid))

            uuid_str_1 = """{0}""".format(process.system_output(cmd))
            uuid_str_2 = """{0}""".format(process.system_output(cmd))
            uuid_str_3 = """{0}""".format(process.system_output(cmd))

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

            delete_cmd_1 = (
                "{0} destroy-pool "
                "-i {1} "
                "-s {2} "
                "-f".format(self.daosctl, uuid_str_1, setid))

            delete_cmd_2 = (
                "{0} destroy-pool "
                "-i {1} "
                "-s {2} "
                "-f".format(self.daosctl, uuid_str_2, setid))

            delete_cmd_3 = (
                "{0} destroy-pool "
                "-i {1} "
                "-s {2} "
                "-f".format(self.daosctl, uuid_str_3, setid))

            process.system(delete_cmd_1)
            process.system(delete_cmd_2)
            process.system(delete_cmd_3)

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
