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

sys.path.append('./util')
import ServerUtils
import CheckForPool

class InfoTests(Test):
    """
    Tests DAOS pool query.

    Note tags not set at present because the test isn't in a working state

    :avocado: tags=rubbish
    """

    # super wasteful since its doing this for every variation
    def setUp(self):
           pass

    def tearDown(self):
           pass

    def test_simple_query(self):
        """
        Test querying a pool created on a single server.

        Note tags not set at present because the test isn't in a working state

        :avocado: tags=rubbish
        """
        # there is a presumption that this test lives in a specific spot
        # in the repo
        basepath = os.path.normpath(os.getcwd() + "../../../../")
        server_group = self.params.get("server_group",'/server/','daos_server')
        hostfile = basepath + self.params.get("hostfile",
                                              '/run/testparams/one_host/')

        ServerUtils.runServer(hostfile, server_group, basepath)

        # not sure I need to do this but ... give it time to start
        time.sleep(1)

        setid = self.params.get("setname",
                                '/run/testparams/setnames/validsetname/')

        mode = self.params.get("mode",'/run/testparams/modes/*',0731)
        size = self.params.get("size",'/run/testparams/sizes/*',0)
        connectperm = self.params.get("perms",'/run/testparams/connectperms/*',
                                      '')

        try:
               # use the uid/gid of the user running the test, these should
               # be perfectly valid
               uid = os.geteuid()
               gid = os.getegid()

               # TODO make these params in the yaml
               daosctl = basepath + '/install/bin/daosctl'

               create_connect_query_cmd = (
                      '{0} test-connect-pool '
                      '-m {1} -u {2} -g {3} -s {4} -z {5} {6}'.
                   format(daosctl, mode, uid, gid, setid, 1, size, connectperm))

               process.system(create_connect_query_cmd)

        except Exception as e:
               print e
               print traceback.format_exc()
               self.fail("Expecting to pass but test has failed.\n")

        # no matter what happens shutdown the server
        finally:
               ServerUtils.stopServer()

if __name__ == "__main__":
    main()
