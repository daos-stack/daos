#!/usr/bin/python
'''
  (C) Copyright 2019 Intel Corporation.

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
from daos_api import DaosPool, DaosApiError

class PoolSecurityTest(TestWithServers):
    """
    DAOS security: initial security tests after pool creation
    :avocado: recursive
    """
    def setUp(self):
        """
        set up method: create 3 pools with different uid and gid
                       for security test.
            pool1: with owner uid, owner-group root-id.
            pool2: with owner rooter-id, owner-group gid.
            pool3: owner and owner-group with both root-id.
        """
        super(PoolSecurityTest, self).setUp()
        root_id = 0
        user_uid = os.geteuid()
        user_gid = os.getegid()
        self.pool1 = self.poolCreate(user_uid, root_id)
        self.pool2 = self.poolCreate(root_id, user_gid)
        self.pool3 = self.poolCreate(root_id, root_id)

    def poolCreate(self, user_uid, user_gid):
        # get parameters from yaml file
        mode = self.params.get("mode", '/run/poolparams/createmode/', 511)
        setid = self.params.get("setname", '/run/poolparams/createset/',
                                "daos_server")
        size = self.params.get("size", '/run/poolparams/createsize/',
                               107374182)
        self.log.info("===Create DAOS pool with:")
        self.log.info("===uid:  %s", user_uid)
        self.log.info("===gid:  %s", user_gid)
        try:
            self.pool = DaosPool(self.context)
            self.pool.create(mode, user_uid, user_gid, size, setid)
            self.log.info("===>Pool create succeed.")
        except DaosApiError as exc:
            self.log.info(exc)
            self.log.info(traceback.format_exc())
            self.fail("##(0)Pool create failed in setUp.\n")
        return self.pool

    def tearDown(self):
        try:
            if self.pool1 is not None:
                self.pool1.disconnect()
                self.pool1.destroy(1)
            if self.pool2 is not None:
                self.pool2.disconnect()
                self.pool2.destroy(1)
            if self.pool3 is not None:
                self.pool3.destroy(1)
        except DaosApiError as exc:
            self.log.info(exc)
            self.log.info(traceback.format_exc())
            self.fail("##(2)Pool destroy failed in tearDown.\n")
        finally:
            super(PoolSecurityTest, self).tearDown()

    def test_poolconnect(self):
        """
        Test basic pool security in pool creation and connect.
        DAOS-2930: DAOS security: initial tests pool creation with default
                   ACL and connect.
        Tests DAOS pools creation, and connect with:
        (1)pool1: match of owner uid.  (succeed)
        (2)pool2: match of owner gid.  (succeed)
        (3)pool3: mismatch of owner uid and gid.
                                       (permission denied with error -1001)

        :avocado: tags=pool,security_basic,security
        """
        user_uid = os.geteuid()
        user_gid = os.getegid()
        der_no_permission = "RC: -1001"
        try:
            self.log.info(
                "===Connecting to the DAOS pool1, match of owner uid.")
            self.log.info("===user_uid:  %s", user_uid)
            self.log.info("===user_gid:  %s", user_gid)
            # connect to the pool with permission
            #   DAOS_PC_RO = int(1 << 0)
            #   DAOS_PC_RW = int(1 << 1)
            #   DAOS_PC_EX = int(1 << 2)
            self.pool1.connect(1 << 1)
            self.log.info("===>(1.1)Connect to the DAOS pool1 succeed.")
        except DaosApiError as exc:
            self.log.info(exc)
            self.log.info(traceback.format_exc())
            self.fail(
                "##(1.1)Connecting to the DAOS pool1 failed.")
        try:
            self.log.info(
                "===Connecting to the DAOS pool2, match of owner gid.")
            self.log.info("===user_uid:  %s", user_uid)
            self.log.info("===user_gid:  %s", user_gid)
            self.pool2.connect(1 << 1)
            self.log.info("===>(1.2)Connect to the DAOS pool2 succeed.")
        except DaosApiError as exc:
            self.log.info(exc)
            self.log.info(traceback.format_exc())
            self.fail(
                "##(1.2)Connecting to the DAOS pool2 failed.")
        try:
            self.log.info(
                "===Connecting to the DAOS pool3, mismatch of uid and gid.")
            self.log.info("===user_uid:  %s", user_uid)
            self.log.info("===user_gid:  %s", user_gid)
            self.pool3.connect(1 << 1)
            self.fail(
                "##(1.3.1)Connecting to the DAOS pool3 succeed, exp fail.")
        except DaosApiError as exc:
            self.log.info(exc)
            self.log.info(traceback.format_exc())
            if der_no_permission not in str(exc):
                self.fail(
                    "##(1.3.2)Expecting error RC: -1001  did not show.")
            self.log.info(
                "===>(1.3)Expected Pool3 connect failed due to no_permission.")
