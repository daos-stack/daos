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

class MultiServerCreateDeleteTest(Test):
    """
    Tests DAOS pool creation, trying both valid and invalid parameters.

    avocado: tags=pool,poolcreate
    """

    # super wasteful since its doing this for every variation
    def setUp(self):
       global urifile
       global hostfile
       global basepath

       basepath = self.params.get("base",'/paths/','rubbish')
       hostfile = basepath + self.params.get("hostfile",'/files/','rubbish')
       urifile = basepath + self.params.get("urifile",'/files/','rubbish')
       server_group = self.params.get("server_group",'/server/','daos_server')

       ServerUtils.runServer(hostfile, urifile, server_group, basepath)
       # not sure I need to do this but ... give it time to start
       time.sleep(1)

    def tearDown(self):
       ServerUtils.stopServer()

    def test_create(self):
        """
        Test basic pool creation.
        """
        global urifile
        global basepath

        # Accumulate a list of pass/fail indicators representing what is expected for
        # each parameter then "and" them to determine the expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode",'/run/tests/modes/*',0731)
        mode = modelist[0]
        expected_for_param.append(modelist[1])

        uidlist  = self.params.get("uid",'/run/tests/uids/*',os.geteuid())
        if uidlist[0] == 'valid':
               uid = os.geteuid()
        else:
               uid = uidlist[0]
        expected_for_param.append(uidlist[1])

        gidlist  = self.params.get("gid",'/run/tests/gids/*',os.getegid())
        if gidlist[0] == 'valid':
               gid = os.getegid()
        else:
               gid = gidlist[0]
        expected_for_param.append(gidlist[1])

        setidlist = self.params.get("setname",'/run/tests/setnames/*',"XXX")
        setid = setidlist[0]
        expected_for_param.append(setidlist[1])

        tgtlistlist = self.params.get("tgt",'/run/tests/tgtlist/*',"")
        tgtlist = tgtlistlist[0]
        expected_for_param.append(tgtlistlist[1])

        # if any parameter is FAIL then the test should FAIL
        expected_result = 'PASS'
        for result in expected_for_param:
               if result == 'FAIL':
                      expected_result = 'FAIL'

        hostfile = basepath + self.params.get("hostfile",'/files/','rubbish')
        host1 = GetHostsFromFile.getHostsFromFile(hostfile)[0]
        host2 = GetHostsFromFile.getHostsFromFile(hostfile)[1]

        try:
            daosctl = basepath + 'install/bin/daosctl'
            orterun = basepath + 'install/bin/orterun'

            cmd = ('{0} --np 1 --ompi-server file:{1} {2} create-pool '
                   '-m {3} -u {4} -g {5} -s {6}'.format(
                          orterun, urifile, daosctl, mode, uid, gid, setid, tgtlist))

            uuid_str = """{0}""".format(process.system_output(cmd))
            print("uuid is {0}\n".format(uuid_str))

            if '0' in tgtlist:
                   exists = CheckForPool.checkForPool(host1, uuid_str)
                   if exists != 0:
                          self.fail("Pool {0} not found on host {1}.\n".format(uuid_str, host1))
            if '1' in tgtlist:
                   exists = CheckForPool.checkForPool(host2, uuid_str)
                   if exists != 0:
                          self.fail("Pool {0} not found on host {1}.\n".format(uuid_str, host2))

            delete_cmd =  ('{0} --np 1 --ompi-server file:{1} {2} destroy-pool '
                           '-i {3} -s {4} -f'.format(orterun, urifile, daosctl,
                                                     uuid_str, setid))

            process.system(delete_cmd)

            if '0' in tgtlist:
                   exists = CheckForPool.checkForPool(host1, uuid_str)
                   if exists == 0:
                          self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str, host1))
            if '1' in tgtlist:
                   exists = CheckForPool.checkForPool(host2, uuid_str)
                   if exists == 0:
                          self.fail("Pool {0} found on host {1} after destroy.\n".format(uuid_str, host2))

            if expected_result in ['FAIL']:
                   self.fail("Test was expected to fail but it passed.\n")

        except Exception as e:
            print e
            print traceback.format_exc()
            if expected_result == 'PASS':
                   self.fail("Test was expected to pass but it failed.\n")

if __name__ == "__main__":
    main()

