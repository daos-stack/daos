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

from avocado.utils import process
from apricot import TestWithServers

class EvictTest(TestWithServers):
    """
    Tests DAOS client eviction from a pool that the client is using.

    :avocado: recursive
    """

    def test_evict(self):
        """
        Test evicting a client to a pool.

        :avocado: tags=pool,poolevict,quick
        """
        size = self.params.get("size", '/run/tests/sizes/size1gb/*')
        setid = self.params.get("setname", '/run/tests/setnames/validsetname/*')
        connectperm = self.params.get("perms",
                                      '/run/tests/connectperms/permro/*')

        try:
            uid = os.geteuid()
            gid = os.getegid()

            create_connect_evict = (
                "{0} test-evict-pool "
                "-m {1} "
                "-u {2} "
                "-g {3} "
                "-s {4} "
                "-z {5} {6} "
                "-l 0".format(self.daosctl, "0731", uid, gid, setid, size,
                              connectperm))
            process.system(create_connect_evict)

        except Exception as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")


    def test_evict_bad_pool(self):
        """
        Test connecting to a pool.

        :avocado: tags=pool,poolevict
        """
        # test parameters are in the EvictTest.yaml
        setid = self.params.get("setname", '/run/tests/setnames/validsetname/*')
        mode = self.params.get("mode", '/run/tests/modes/modeall/*')
        uid = os.geteuid()
        gid = os.getegid()

        try:
            create_cmd = (
                "{0} create-pool "
                "-m {1} "
                "-u {2} "
                "-g {3} "
                "-s {4} "
                "-c 1".format(self.daosctl, mode, uid, gid, setid))
            uuid_str = """{0}""".format(process.system_output(create_cmd))
            print("uuid is {0}\n".format(uuid_str))

        except Exception as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Unexpected pool create and connect failure.\n")
        try:
            # use the wrong uuid, which of course should fail
            bogus_uuid = "44be695840f4b6eda581b461a4d4c570490ba4db"
            bad_evict_cmd = (
                "{0} evict-pool "
                "-i {1} "
                "-s {2}".format(self.daosctl, bogus_uuid, setid))
            process.system(bad_evict_cmd)

        except Exception as excep:
            # this section of the test should fail and throw an exception
            print("command expected to fail")

        try:
            # use the wrong server group name but the correct uuid
            bogus_server_group = self.params.get("setname",
                                                 '/run/tests/setnames/'
                                                 'badsetname/*')
            bad_evict_cmd = (
                "{0} evict-pool "
                "-i {1} "
                "-s {2}".format(self.daosctl, uuid_str, bogus_server_group))
            process.system(bad_evict_cmd)

        except Exception as excep:
            # this section of the test should fail and throw an exception
            print("command expected to fail")

        try:
            # evict for real, there are no client connections so not
            # really necessary
            good_evict_cmd = (
                "{0} evict-pool "
                "-i {1} "
                "-s {2} "
                "-l 0".format(self.daosctl, uuid_str, setid))
            process.system(good_evict_cmd)

            delete_cmd = (
                "{0} destroy-pool "
                "-i {1} "
                "-s {2} -f".format(self.daosctl, uuid_str, setid))

            process.system(delete_cmd)

        # this time nothing should go wrong and its an error if it does
        except Exception as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Expecting to pass but test has failed.\n")
