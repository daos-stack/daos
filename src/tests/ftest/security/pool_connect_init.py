#!/usr/bin/python3
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import traceback

from apricot import TestWithServers
from avocado.core.exceptions import TestFail


class PoolSecurityTest(TestWithServers):
    """
    DAOS security: initial security tests after pool creation

    :avocado: recursive
    """

    def test_pool_connect(self):
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

        :avocado: tags=all,daily_regression
        :avocado: tags=small
        :avocado: tags=pool,sec_basic,security
        """
        der_no_permission = "RC: -1001"
        user_uid = os.geteuid()
        user_gid = os.getegid()

        self.add_pool(create=False)

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
