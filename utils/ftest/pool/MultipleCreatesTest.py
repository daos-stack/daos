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

def printFunc(thestring):
       print "<SERVER>" + thestring

session = None
hostfile = ""
urifile = ""


class MultipleCreatesTest(Test):
    """
    Tests DAOS pool creation, calling it repeatedly one after another

    avocado: tags=pool,poolcreate
    """

    # super wasteful since its doing this for every variation
    def setUp(self):
       global session
       global urifile
       global hostfile

       hostfile = self.params.get("hostfile",'/files/','rubbish')
       urifile = self.params.get("urifile",'/files/','rubbish')

       ServerUtils.runServer(hostfile, urifile)
       # not sure I need to do this but ... give it time to start
       time.sleep(2)

    def tearDown(self):
       global session
       ServerUtils.stopServer()

    def test_create_one(self):
        """
        Test issuing a single  pool create commands at once.
        """
        global urifile

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
               cmd = ('../../install/bin/orterun -np 1 '
                      '--ompi-server file:{0} ./pool/wrapper/SimplePoolTests {1} {2} {3} {4} {5}'.format(
                          urifile, "create", mode, uid, gid, setid))
               uuid_str = """{0}""".format(process.system_output(cmd))
               print("uuid is {0}\n".format(uuid_str))

               exists = CheckForPool.checkForPool('vm1', uuid_str)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str, 'vm1'))

               delete_cmd =  ('../../install/bin/orterun -np 1 '
                              '--ompi-server file:{0} ./pool/wrapper/SimplePoolTests {1} {2} {3} {4}'.format(
                                     urifile, "destroy", uuid_str, setid, "1"))

               process.system(delete_cmd)


               exists = CheckForPool.checkForPool('vm1', uuid_str)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str, 'vm1'))

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
               cmd = ('../../install/bin/orterun -np 1 '
                      '--ompi-server file:{0} ./pool/wrapper/SimplePoolTests {1} {2} {3} {4} {5}'.format(
                          urifile, "create", mode, uid, gid, setid))

               uuid_str_1 = """{0}""".format(process.system_output(cmd))
               uuid_str_2 = """{0}""".format(process.system_output(cmd))

               exists = CheckForPool.checkForPool('vm1', uuid_str_1)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_1, 'vm1'))
               exists = CheckForPool.checkForPool('vm1', uuid_str_2)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_2, 'vm1'))


               delete_cmd_1 =  ('../../install/bin/orterun -np 1 '
                              '--ompi-server file:{0} ./pool/wrapper/SimplePoolTests {1} {2} {3} {4}'.format(
                                     urifile, "destroy", uuid_str_1, setid, "1"))

               delete_cmd_2 =  ('../../install/bin/orterun -np 1 '
                              '--ompi-server file:{0} ./pool/wrapper/SimplePoolTests {1} {2} {3} {4}'.format(
                                     urifile, "destroy", uuid_str_2, setid, "1"))

               process.system(delete_cmd_1)
               process.system(delete_cmd_2)

               exists = CheckForPool.checkForPool('vm1', uuid_str_1)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str_1, 'vm1'))
               exists = CheckForPool.checkForPool('vm1', uuid_str_2)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str_2, 'vm1'))


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
               cmd = ('../../install/bin/orterun -np 1 '
                      '--ompi-server file:{0} ./pool/wrapper/SimplePoolTests {1} {2} {3} {4} {5}'.format(
                          urifile, "create", mode, uid, gid, setid))

               uuid_str_1 = """{0}""".format(process.system_output(cmd))
               uuid_str_2 = """{0}""".format(process.system_output(cmd))
               uuid_str_3 = """{0}""".format(process.system_output(cmd))


               exists = CheckForPool.checkForPool('vm1', uuid_str_1)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_1, 'vm1'))
               exists = CheckForPool.checkForPool('vm1', uuid_str_2)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_2, 'vm1'))
               exists = CheckForPool.checkForPool('vm1', uuid_str_3)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".format(uuid_str_3, 'vm1'))

               delete_cmd_1 =  ('../../install/bin/orterun -np 1 '
                              '--ompi-server file:{0} ./pool/wrapper/SimplePoolTests {1} {2} {3} {4}'.format(
                                     urifile, "destroy", uuid_str_1, setid, "1"))

               delete_cmd_2 =  ('../../install/bin/orterun -np 1 '
                              '--ompi-server file:{0} ./pool/wrapper/SimplePoolTests {1} {2} {3} {4}'.format(
                                     urifile, "destroy", uuid_str_2, setid, "1"))

               delete_cmd_3 =  ('../../install/bin/orterun -np 1 '
                              '--ompi-server file:{0} ./pool/wrapper/SimplePoolTests {1} {2} {3} {4}'.format(
                                     urifile, "destroy", uuid_str_3, setid, "1"))

               process.system(delete_cmd_1)
               process.system(delete_cmd_2)
               process.system(delete_cmd_3)

               exists = CheckForPool.checkForPool('vm1', uuid_str_1)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str_1, 'vm1'))
               exists = CheckForPool.checkForPool('vm1', uuid_str_2)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str_2, 'vm1'))
               exists = CheckForPool.checkForPool('vm1', uuid_str_3)
               if exists == 0:
                      self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str_3, 'vm1'))

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

