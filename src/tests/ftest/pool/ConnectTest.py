#!/usr/bin/python
'''
  (C) Copyright 2018 Intel Corporation.

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

class ConnectTest(Test):
    """
    Tests DAOS pool creation, calling it repeatedly one after another

    :avocado: tags=pool,poolconnect
    """

    # super wasteful since its doing this for every variation
    def setUp(self):
       global hostfile
       global basepath

       # there is a presumption that this test lives in a specific spot in
       # the repo
       basepath = os.path.normpath(os.getcwd() + "../../../../")

       hostfile = basepath + self.params.get("hostfile",'/files/','rubbish')
       server_group = self.params.get("server_group",'/server/','daos_server')

       ServerUtils.runServer(hostfile, server_group, basepath)

       # not sure I need to do this but ... give it time to start
       time.sleep(1)

    def tearDown(self):
       ServerUtils.stopServer()

    def test_connect(self):
        """
        Test connecting to a pool.

        :avocado: tags=pool,poolconnect,quick
        """
        global basepath

        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        setidlist = self.params.get("setname",'/run/tests/setnames/*')
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

               # TODO make these params in the yaml
               daosctl = basepath + '/install/bin/daosctl'

               hostfile = basepath + self.params.get("hostfile",'/run/files/')
               host1 = GetHostsFromFile.getHostsFromFile(hostfile)[0]
               host2 = GetHostsFromFile.getHostsFromFile(hostfile)[1]

               create_cmd = (
                   '{0} create-pool -m {1} -u {2} -g {3} -s {4}'.format(
                            daosctl, 0731, uid, gid, setid))
               uuid_str = """{0}""".format(process.system_output(create_cmd))
               print("uuid is {0}\n".format(uuid_str))

               exists = CheckForPool.checkForPool(host1, uuid_str)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".
                                format(uuid_str, host1))
               exists = CheckForPool.checkForPool(host2, uuid_str)
               if exists != 0:
                      self.fail("Pool {0} not found on host {1}.\n".
                                format(uuid_str, host2))

               connect_cmd = (
                   '{0} connect-pool -i {1} -s {2} -r'.format(daosctl,
                   uuid_str, setid))
               process.system(connect_cmd)

               delete_cmd = ('{0} destroy-pool -i {1} -s {2} -f'.format(
                   daosctl, uuid_str, setid))

               if expected_result == 'FAIL':
                      self.fail("Expected to fail but passed.\n")

        except Exception as e:
               print e
               print traceback.format_exc()
               if expected_result == 'PASS':
                      self.fail("Expecting to pass but test has failed.\n")

if __name__ == "__main__":
    main()
