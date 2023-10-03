'''
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import grp

import security_test_base as secTestBase
from pool_security_test_base import PoolSecurityTestBase

PERMISSIONS = ["", "r", "w", "rw"]


class SecurityPoolGroupsTest(PoolSecurityTestBase):
    """Test daos_pool acl for primary and secondary groups.

    :avocado: recursive
    """

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
            different test scenarios

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=security,pool
        :avocado: tags=SecurityPoolGroupsTest,pool_acl,sec_acl_groups,test_daos_pool_acl_groups
        '''
        user_gid = os.getegid()
        current_group = grp.getgrgid(user_gid)[0]
        primary_grp_perm = self.params.get(
            "pg_permission", "/run/pool_acl/primary_secondary_group_test/*")[0]
        read, write = self.params.get(
            "pg_read_write", "/run/pool_acl/primary_secondary_group_test/*")
        acl_entries = ["", "", "",
                       secTestBase.acl_entry("group", current_group, primary_grp_perm,
                                             PERMISSIONS), ""]
        if primary_grp_perm.lower() == "none":
            primary_grp_perm = ""
        if primary_grp_perm not in PERMISSIONS:
            self.fail("##primary_grp_perm {} is invalid, valid permissions are:"
                      "'none', 'r', w', 'rw'".format(primary_grp_perm))

        self.log.info("==Starting self.pool_acl_verification")
        self.log.info(" =acl_entries = %s", acl_entries)
        self.log.info(" =expect read = %s", read)
        self.log.info(" =expect write= %s", write)
        self.pool_acl_verification(acl_entries, read, write, True)
        self.log.info(
            "--->Testcase Passed. "
            " user_type: group, permission: %s, expect_read: %s,"
            " expect_write: %s.\n", primary_grp_perm, read, write)
