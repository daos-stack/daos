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
import daos_api

def printFunc(thestring):
       print("<SERVER>" + thestring)

class RebuildTests(Test):

    """
    Test Class Description:
    This class contains tests for pool rebuild.
    """

    def setUp(self):
           global basepath
           global server_group
           basepath = self.params.get("base",'/paths/','rubbish')
           server_group = self.params.get("server_group",'/server/',
                                          'daos_server')

    def tearDown(self):
           pass

    def test_simple_rebuild(self):
        """
        Test ID: Rebuild-001
        Test Description: The most basic rebuild test.
        Use Cases: list of use cases that are to at least some degree covered
        by this test case
        :avocado: tags=regression,pool,rebuild
        """

        global basepath
        global server_group

        hostfile = basepath
        urifile = basepath
        orterun = basepath
        daosctl = basepath
        hostfile += self.params.get("hostfile",'/run/testparams/four_hosts/')
        urifile += self.params.get("urifile",'/run/testparams/urifiles/')
        orterun += self.params.get("orterun", '/run/binaries/')
        daosctl += self.params.get("daosctl", '/run/binaries/')

        ServerUtils.runServer(hostfile, urifile, server_group, basepath)

        # not sure I need to do this but ... give it time to start
        time.sleep(1)

        setid = self.params.get("setname",
                                '/run/testparams/setnames/validsetname/')

        try:
               # use the uid/gid of the user running the test, these should
               # be perfectly valid
               uid = os.geteuid()
               gid = os.getegid()

               create_cmd = ('{0} --np 1 --ompi-server file:{1} '.
                             format(orterun, urifile) +
                             '{0} create-pool -u {1} -g {2} -s {3}'.format(
                             daosctl, uid, gid, setid))
               uuid_str = """{0}""".format(process.system_output(create_cmd))

               time.sleep(20);

               # show the rebuild status
               status_cmd = ('{0} --np 1 --ompi-server file:{1} '.
                             format(orterun, urifile) +
                             '{0} query-pool-status -i {1} -s {2}'.format(
                             daosctl, uuid_str, setid) +
                             ' -g {0} -u {1} -l 1'.format(gid, uid))

               status_str = """{0}""".format(process.system_output(status_cmd))
               print(status_str)

               # kill a server, there are 4 so kill rank 3
               kill_cmd = ('{0} --np 1 --ompi-server file:{1} '.
                             format(orterun, urifile) +
                             '{0} kill-server -f -l {1} -s {2}'.format(
                             daosctl, '3', setid))
               process.system_output(kill_cmd)

               # temporarily, the exclude of a failed target must be done
               # manually
               exclude_cmd = ('{0} --np 1 --ompi-server file:{1} '.
                             format(orterun, urifile) +
                             '{0} exclude-target -i {1} -s {2}'.format(
                             daosctl, uuid_str, setid) +
                             ' -l {0} -t {1}'.format(1, 3))
               print("running: %s\n", exclude_cmd)
               exclude_str = """{0}""".format(
                      process.system_output(exclude_cmd))
               print(exclude_str)

               time.sleep(60);

               # show the rebuild status again
               status_cmd = ('{0} --np 1 --ompi-server file:{1} '.
                             format(orterun, urifile) +
                             '{0} query-pool-status -i {1} -s {2}'.format(
                             daosctl, uuid_str, setid) +
                             ' -g {0} -u {1} -l 1'.format(gid, uid))
               print("running: %s\n", status_cmd)
               status_str = """{0}""".format(process.system_output(status_cmd))
               print(status_str)

        except Exception as e:
               print(e)
               print(traceback.format_exc())
               self.fail("Expecting to pass but test has failed.\n")

        # no matter what happens shutdown the server
        finally:
               ServerUtils.stopServer()

if __name__ == "__main__":
    main()
