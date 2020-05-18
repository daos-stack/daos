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

import os
import grp
#import pool_security_test_base as poolSec
import security_test_base as secTestBase
from pool_security_test_base import PoolSecurityTestBase

PERMISSIONS = ["", "r", "w", "rw"]

class DaosRunPoolSecurityTest(PoolSecurityTestBase):
    """Test daos_pool acl for primary and secondary groups.

    :avocado: recursive
    """
    # pylint: disable=too-many-ancestors

    def test_daos_pool_acl_groups(self):
        '''
        Epic:
            DAOS-1961: Testing related to DAOS security features.
        Testcase description:
            DAOS-2951: Pool ACE/ACL identities verification.
            DAOS-2955: Primary group user ACL verification.
            DAOS-2957: Secondary group user ACL verification.
            DAOS-3546: Test ACL access when user/group management
                       isn't synced between client and server nodes.
            DAOS-2961: DAOS dmg pool overwrite-acl verification
            DAOS-2962: DAOS dmg pool delete-acl verification
            DAOS-2963: DAOS dmg pool get-acl verification
        Description:
            Create pool with pass-in user on primary and secondary group
            acl permission, verify pool user and group read, write, read-write
            and none permissions enforcement with all forms of input under
            different test sceanrios.
        :avocado: tags=all,full_regression,security,pool_acl,sec_acl_groups
        '''
        user_gid = os.getegid()
        current_group = grp.getgrgid(user_gid)[0]
        primary_grp_perm = self.params.get(\
            "pg_permission", "/run/pool_acl/primary_secondary_group_test/*")[0]
        read, write = self.params.get(\
            "pg_read_write", "/run/pool_acl/primary_secondary_group_test/*")
        acl_entries = ["", "", "",\
            secTestBase.acl_entry("group", current_group, primary_grp_perm, PERMISSIONS), ""]
        if primary_grp_perm.lower() == "none":
            primary_grp_perm = ""
        if primary_grp_perm not in PERMISSIONS:
            self.fail("##primary_grp_perm %s is invalid, valid permissions are:"
                      "'none', 'r', w', 'rw'", primary_grp_perm)

        self.log.info("==Starting self.pool_acl_verification")
        self.log.info(" =acl_entries = %s", acl_entries)
        self.log.info(" =expect read = %s", read)
        self.log.info(" =expect write= %s", write)
        self.pool_acl_verification(acl_entries, read, write, True)
        self.log.info(
            "--->Testcase Passed. "
            " user_type: group, permission: %s, expect_read: %s,"
            " expect_write: %s.\n", primary_grp_perm, read, write)
