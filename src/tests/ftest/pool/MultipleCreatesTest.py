#!/usr/bin/python
'''
  (C) Copyright 2017 Intel Corporation.

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

import os
import time
import traceback
import sys

from avocado       import Test
from avocado       import main
from avocado.utils import process
from avocado.utils import git

import aexpect
from aexpect.client import run_bg

sys.path.append('./util')
import ServerUtils
import CheckForPool
import GetHostsFromFile

def printFunc(thestring):
       print "<TESTCLIENT>" + thestring

sessions = {}
hostfile = ""
urifile = ""


class MultipleCreatesTest(Test):
    """
    Tests DAOS pool creation, calling it repeatedly one after another

    avocado: tags=pool,poolcreate
    """

    # super wasteful since its doing this for every variation
    def setUp(self):
       global sessions
       global urifile
       global hostfile
       global basepath

       basepath = self.params.get("base",'/paths/','rubbish')
       hostfile = basepath + self.params.get("hostfile",'/files/local/','rubbish')
       urifile = basepath + self.params.get("urifile",'/files/local/','rubbish')
       server_group = self.params.get("server_group",'/server/','daos_server')

       ServerUtils.runServer(hostfile, urifile, server_group, basepath)
       # not sure I need to do this but ... give it time to start
       time.sleep(2)

    def tearDown(self):
       global sessions
       ServerUtils.stopServer()

    def test_create_one(self):
        """
        Test issuing a single  pool create commands at once.
        """
        global urifile
        global basepath

        # Accumulate a list of pass/fail indicators representing what is expected for
        # each parameter then "and" them to determine the expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode",'/run/tests/modes/*')
        mode = modelist[0]
        expected_for_param.append(modelist[1])

        setidlist = self.params.get("setname",'/run/tests/setnames/*')
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
               orterun = basepath + 'install/bin/orterun'
               daosctl = basepath + 'install/bin/daosctl'
               cmd = ('{0} --np 1 --ompi-server file:{1} '
                      '{2} create-pool -m {3} -u {4} -g {5} -s {6}'.format(orterun,
                          urifile, daosctl, mode, uid, gid, setid))
               uuid_str = """{0}""".format(process.system_output(cmd))
               print("uuid is {0}\n".format(uuid_str))

               hostfile = basepath + self.params.get("hostfile",'/run/files/local/')
               host = GetHostsFromFile.getHostsFromFile(hostfile)[0]
               exists = CheckForPool.checkForPool(host, uuid_str)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str, host))

               delete_cmd =  ('{0} --np 1 --ompi-server file:{1} {2} destroy-pool '
                              '-i {3} -s {4} -f'.format(orterun, urifile, daosctl,
                                                        uuid_str, setid))

               process.system(delete_cmd)


               exists = CheckForPool.checkForPool(host, uuid_str)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str, host))

               if expected_result == 'FAIL':
                      self.fail("Expected to fail but passed.\n")

        except Exception as e:
               print e
               print traceback.format_exc()
               if expected_result == 'PASS':
                      self.fail("Expecting to pass but test has failed.\n")


    def test_create_two(self):
        """
        Test issuing multiple pool create commands at once.
        """
        global urifile
        global basepath

        # Accumulate a list of pass/fail indicators representing what is expected for
        # each parameter then "and" them to determine the expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode",'/run/tests/modes/*')
        mode = modelist[0]
        expected_for_param.append(modelist[1])

        setidlist = self.params.get("setname",'/run/tests/setnames/*')
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
               orterun = basepath + 'install/bin/orterun'
               daosctl = basepath + 'install/bin/daosctl'
               cmd = ('{0} --np 1 --ompi-server file:{1} {2} create-pool'
                      ' -m {3} -u {4} -g {5} -s {6}'.format(
                          orterun, urifile, daosctl, mode, uid, gid, setid))

               uuid_str_1 = """{0}""".format(process.system_output(cmd))
               uuid_str_2 = """{0}""".format(process.system_output(cmd))

               hostfile = basepath + self.params.get("hostfile",'/run/files/local/')
               host = GetHostsFromFile.getHostsFromFile(hostfile)[0]
               exists = CheckForPool.checkForPool(host, uuid_str_1)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_1, host))
               exists = CheckForPool.checkForPool(host, uuid_str_2)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_2, host))

               delete_cmd_1 =  ('{0} --np 1 --ompi-server file:{1} {2} destroy-pool '
                                ' -i {3} -s {4} -f'.format(orterun,
                                     urifile, daosctl, uuid_str_1, setid))

               delete_cmd_2 =  ('{0} --np 1 --ompi-server file:{1} {2} destroy-pool '
                                ' -i {3} -s {4} -f'.format(orterun, urifile, daosctl,
                                                           uuid_str_2, setid))

               process.system(delete_cmd_1)
               process.system(delete_cmd_2)

               exists = CheckForPool.checkForPool(host, uuid_str_1)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str_1, host))
               exists = CheckForPool.checkForPool(host, uuid_str_2)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str_2, host))

               if expected_result == 'FAIL':
                      self.fail("Expected to fail but passed.\n")

        except Exception as e:
               print e
               print traceback.format_exc()
               if expected_result == 'PASS':
                      self.fail("Expecting to pass but test has failed.\n")


    def test_create_three(self):
        """
        Test issuing multiple pool create commands at once.
        """
        global urifile
        global basepath

        # Accumulate a list of pass/fail indicators representing what is expected for
        # each parameter then "and" them to determine the expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode",'/run/tests/modes/*')
        mode = modelist[0]
        expected_for_param.append(modelist[1])

        setidlist = self.params.get("setname",'/run/tests/setnames/*')
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
               orterun = basepath + 'install/bin/orterun'
               daosctl = basepath + 'install/bin/daosctl'

               cmd = ('{0} --np 1 --ompi-server file:{1} {2} create-pool '
                      '-m {3} -u {4} -g {5} -s {6}'.format(orterun,
                              urifile, daosctl, mode, uid, gid, setid))

               uuid_str_1 = """{0}""".format(process.system_output(cmd))
               uuid_str_2 = """{0}""".format(process.system_output(cmd))
               uuid_str_3 = """{0}""".format(process.system_output(cmd))

               # TODO: horrible hard-coded hostname needs to be fixed
               hostfile = basepath + self.params.get("hostfile",'/run/files/local/')
               host = GetHostsFromFile.getHostsFromFile(hostfile)[0]
               exists = CheckForPool.checkForPool(host, uuid_str_1)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_1, host))
               exists = CheckForPool.checkForPool(host, uuid_str_2)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_2, host))
               exists = CheckForPool.checkForPool(host, uuid_str_3)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_3, host))

               delete_cmd_1 =  ('{0} --np 1 --ompi-server file:{1} {2} destroy-pool '
                                '-i {3} -s {4} -f'.format(orterun, urifile, daosctl,
                                                          uuid_str_1, setid))

               delete_cmd_2 =  ('{0} --np 1 --ompi-server file:{1} {2} destroy-pool '
                                '-i {3} -s {4} -f'.format(orterun, urifile, daosctl,
                                                          uuid_str_2, setid))

               delete_cmd_3 =  ('{0} --np 1 --ompi-server file:{1} {2} destroy-pool '
                                '-i {3} -s {4} -f'.format(orterun, urifile, daosctl,
                                                          uuid_str_3, setid))

               process.system(delete_cmd_1)
               process.system(delete_cmd_2)
               process.system(delete_cmd_3)

               exists = CheckForPool.checkForPool(host, uuid_str_1)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str_1, host))
               exists = CheckForPool.checkForPool(host, uuid_str_2)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str_2, host))
               exists = CheckForPool.checkForPool(host, uuid_str_3)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str_3, host))

               if expected_result == 'FAIL':
                      self.fail("Expected to fail but passed.\n")

        except Exception as e:
               print e
               print traceback.format_exc()
               if expected_result == 'PASS':
                      self.fail("Expecting to pass but test has failed.\n")

    # COMMENTED OUT because test environments don't always have enough memory to run this
    #def test_create_five(self):
    #    """
    #    Test issuing five pool create commands at once.
    #    """
    #    global urifile
    #
    #    # Accumulate a list of pass/fail indicators representing what is expected for
    #    # each parameter then "and" them to determine the expected result of the test
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
    #                  '--ompi-server file:{0} ./pool/wrapper/SimplePoolTests {1} {2} {3} {4} {5}'.format(
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
    #           print e
    #           print traceback.format_exc()
    #           if expected_result == 'PASS':
    #                  self.fail("Expecting to pass but test has failed.\n")

if __name__ == "__main__":
    main()
