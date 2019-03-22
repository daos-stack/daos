#!/usr/bin/python
'''
  (C) Copyright 2017-2019 Intel Corporation.

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
import sys
import json

from avocado       import Test
from avocado.utils import process

sys.path.append('./util')
import server_utils
import check_for_pool
import write_host_file

class MultiServerCreateDeleteTest(Test):
    """
    Tests DAOS pool creation, trying both valid and invalid parameters.
    """
    # super wasteful since its doing this for every variation
    def setUp(self):
        # there is a presumption that this test lives in a specific spot
        # in the repo
        # get paths from the build_vars generated by build
        self.hostlist = None
        with open('../../../.build_vars.json') as f_open:
            build_paths = json.load(f_open)
        basepath = os.path.normpath(build_paths['PREFIX']  + "/../")
        self.hostlist = self.params.get("test_machines", '/run/hosts/')
        self.hostfile = write_host_file.write_host_file(self.hostlist,
                                                        self.workdir)
        server_group = self.params.get("server_group", '/server/',
                                       'daos_server')

        server_utils.run_server(self.hostfile, server_group, basepath)
        self.dmg = basepath + '/install/bin/dmg'

    def tearDown(self):
        server_utils.stop_server(hosts=self.hostlist)

    def test_create(self):
        """
        Test basic pool creation.
        :avocado: tags=pool,poolcreate,multitarget
        """
        # Accumulate a list of pass/fail indicators representing what is
        # expected for each parameter then "and" them to determine the
        # expected result of the test
        expected_for_param = []

        modelist = self.params.get("mode", '/run/tests/modes/*')
        mode = modelist[0]

        expected_for_param.append(modelist[1])

        uidlist = self.params.get("uid", '/run/tests/uids/*')
        if uidlist[0] == 'valid':
            uid = os.geteuid()
        else:
            uid = uidlist[0]
        expected_for_param.append(uidlist[1])

        gidlist = self.params.get("gid", '/run/tests/gids/*')
        if gidlist[0] == 'valid':
            gid = os.getegid()
        else:
            gid = gidlist[0]
        expected_for_param.append(gidlist[1])

        setidlist = self.params.get("setname", '/run/tests/setnames/*')
        setid = setidlist[0]
        expected_for_param.append(setidlist[1])

        tgtlistlist = self.params.get("tgt", '/run/tests/tgtlist/*')
        tgtlist = tgtlistlist[0]
        expected_for_param.append(tgtlistlist[1])

        # if any parameter is FAIL then the test should FAIL
        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        host1 = self.hostlist[0]
        host2 = self.hostlist[1]

        try:
            cmd = (
                "{} create "
                "--mode={} "
                "--uid={} "
                "--gid={} "
                "--group={} "
                "--target={}".format(self.dmg, mode, uid, gid, setid, tgtlist))

            uuid_str = (
                """{0}""".format(process.system_output(cmd)).split(" ")[0])
            print("uuid is {0}\n".format(uuid_str))

            if '0' in tgtlist:
                exists = check_for_pool.check_for_pool(host1, uuid_str)
                if exists != 0:
                    self.fail("Pool {0} not found on host {1}.\n"
                              .format(uuid_str, host1))
            if '1' in tgtlist:
                exists = check_for_pool.check_for_pool(host2, uuid_str)
                if exists != 0:
                    self.fail("Pool {0} not found on host {1}.\n"
                              .format(uuid_str, host2))

            delete_cmd = (
                "{0} destroy "
                "--pool={1} "
                "--group={2} "
                "--force".format(self.dmg, uuid_str, setid))

            process.system(delete_cmd)

            if '0' in tgtlist:
                exists = check_for_pool.check_for_pool(host1, uuid_str)
                if exists == 0:
                    self.fail("Pool {0} found on host {1} after destroy.\n"
                              .format(uuid_str, host1))
            if '1' in tgtlist:
                exists = check_for_pool.check_for_pool(host2, uuid_str)
                if exists == 0:
                    self.fail("Pool {0} found on host {1} after destroy.\n"
                              .format(uuid_str, host2))

            if expected_result in ['FAIL']:
                self.fail("Test was expected to fail but it passed.\n")

        except Exception as exc:
            print(exc)
            print(traceback.format_exc())
            if expected_result == 'PASS':
                self.fail("Test was expected to pass but it failed.\n")
