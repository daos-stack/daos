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
       print "<TESTCLIENT>" + thestring

session = None
hostfile = ""
urifile = ""


class SimpleCreateDeleteTest(Test):
    """
    Tests DAOS pool creation, trying both valid and invalid parameters.

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

    def test_create(self):
        """
        Test basic pool creation.
        """
        global urifile

        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
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

        # if any parameter is FAIL then the test should FAIL
        expected_result = 'PASS'
        for result in expected_for_param:
               if result == 'FAIL':
                      expected_result = 'FAIL'
                      break

        try:
            cmd = ('../../install/bin/orterun --np 1 '
                   '--ompi-server file:{0} ./pool/wrapper/SimplePoolTests {1} {2} {3} {4} {5}'.format(
                          urifile, "candd", mode, uid, gid, setid))
            process.system(cmd)

            if expected_result in ['FAIL']:
                   self.fail("Test was expected to fail but it passed.\n")

        except Exception as e:
            print e
            print traceback.format_exc()
            if expected_result == 'PASS':
                   self.fail("Test was expected to pass but it failed.\n")

if __name__ == "__main__":
    main()

