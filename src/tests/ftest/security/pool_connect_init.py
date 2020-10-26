#!/usr/bin/python
'''
  (C) Copyright 2020 Intel Corporation.

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

from apricot import TestWithServers
from avocado.core.exceptions import TestFail
from test_utils_pool import TestPool


class PoolSecurityTest(TestWithServers):
    """
    DAOS security: initial security tests after pool creation

    :avocado: recursive
    """

    def test_poolconnect(self):
        """
        Test basic pool security in pool creation and connect.
        DAOS-2930: DAOS security: initial tests pool creation with default
                   ACL and connect.
        Tests DAOS pools creation, and connect with:
           (1)pool match of owner uid.  (succeed)
           (2)pool match of owner gid.  (succeed)
           (3)pool mismatch of owner uid and gid.
                                      (permission denied with error -1001)
           Above 3 testcases are defined in the yaml file.

        :avocado: tags=all,pr,full_regression,small,pool,sec_basic,security
        """
        der_no_permission = "RC: -1001"
        user_uid = os.geteuid()
        user_gid = os.getegid()

        self.pool = TestPool(self.context, self.get_dmg_command())
        self.pool.get_params(self)

        uid, gid, expected = self.params.get("ids", "/run/pool/tests/*")

        if uid != "owner":
            self.pool.uid = uid
        if gid != "owner":
            self.pool.gid = gid
        self.log.info("==>   Creating a pool with:")
        self.log.info("      user :       %s", uid)
        self.log.info("      group:       %s", gid)
        self.pool.create()
        self.log.info("==>   Connecting the pool with:")
        self.log.info("      user-owner : %s", user_uid)
        self.log.info("      group-owner: %s", user_gid)
        self.log.info("      expecting:   %s", expected)
        if expected == "FAIL":
            try:
                self.pool.connect()
            except TestFail as exc:
                self.log.info(exc)
                self.log.info(traceback.format_exc())
                if der_no_permission not in str(exc):
                    self.fail(
                        "##Expecting error RC: -1001  did not show.")
                self.log.info(
                    "===>Expected Pool connect failed due to no_permission.")
        else:
            self.pool.connect()
