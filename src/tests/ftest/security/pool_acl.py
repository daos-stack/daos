'''
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
import pwd
import grp

import security_test_base as secTestBase
from pool_security_test_base import PoolSecurityTestBase

PERMISSIONS = ["", "r", "w", "rw"]


class SecurityPoolACLTest(PoolSecurityTestBase):
    """Test daos_pool acl for primary and secondary groups.

    :avocado: recursive
    """

    def test_daos_pool_acl_enforcement(self):
        """
        Epic:
            DAOS-1961: Testing related to DAOS security features.
        Testcase description:
            DAOS-2950: New pool create with pass-in ACL and connect
                       credential verification.
            DAOS-2952: Pool ACE/ACL permissions verification.
            DAOS-2953: Pool ACL enforcement order verification.
            DAOS-3611: Pool ACL verification after user removed from a
                       granted group.
            DAOS-3612: ACL update to remove/grant user/group access pool
            DAOS-3546: Test ACL access when user/group management isn't
                       synced between client and server nodes
        Description:
            Create pool with pass-in user and group acl permission,
            verify pool user and group read, write, read-write and none
            permissions enforcement with all forms of input under different
            test scenarios

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=security,pool
        :avocado: tags=SecurityPoolACLTest,pool_acl,sec_acl,test_daos_pool_acl_enforcement
        """
        user_uid = os.geteuid()
        user_gid = os.getegid()
        current_user = pwd.getpwuid(user_uid)[0]
        current_group = grp.getgrgid(user_gid)[0]
        user_type = self.params.get("user", "/run/pool_acl/*")[0].lower()
        permission, read, write = self.params.get("name", "/run/pool_acl/*")

        user_types = ["owner", "user", "ownergroup", "group", "everyone"]
        default_acl_entries = ["A::OWNER@:",
                               secTestBase.acl_entry("user", current_user, "",
                                                     PERMISSIONS),
                               "A:G:GROUP@:",
                               secTestBase.acl_entry("group", current_group, "",
                                                     PERMISSIONS),
                               "A::EVERYONE@:"]
        test_acl_entries = ["", "", "", "", ""]

        if permission.lower() == "none":
            permission = ""
        if permission not in PERMISSIONS:
            self.fail(
                "##permission {} is invalid, valid permissions are:"
                "'none', 'r', w', 'rw'".format(permission))
        if user_type not in user_types:
            self.fail(
                "##user_type {} is invalid, valid user_types are: "
                "{}".format(user_type, user_types))
        user_type_ind = user_types.index(user_type)
        self.log.info("===>Start DAOS pool acl enforcement order Testcase: "
                      " user_type: %s, permission: %s, expect_read: %s,"
                      " expect_write: "
                      "%s.", user_type, permission, read, write)
        # take care of the user_type which have higher privilege
        for ind in range(5):
            if ind < user_type_ind:
                test_acl_entries[ind] = ""
            elif ind == user_type_ind:
                continue
            else:
                test_acl_entries[ind] = default_acl_entries[ind]
        test_permission = permission
        # take care of rest of the user-type permission
        group_acl = ""
        for ind in range(user_type_ind, 5):
            if ind != user_type_ind:
                # setup opposite test_permission with permission
                test_permission = "rw"
                if permission == "rw":
                    test_permission = ""
                if user_types[ind] == "group":
                    group_acl = test_permission
            test_acl_entries[ind] = default_acl_entries[ind] + test_permission
        # union of "ownergroup" and group permission
        if user_type == "ownergroup":
            if permission != group_acl:
                union_acl = "".join(list(set().union(permission, group_acl)))
                if union_acl == "":
                    read = "deny"
                    write = "deny"
                elif union_acl == "r":
                    read = "pass"
                    write = "deny"
                elif union_acl == "w":
                    read = "deny"
                    write = "deny"
                else:
                    read = "pass"
                    write = "pass"
        self.pool_acl_verification(test_acl_entries, read, write)
        self.log.info(
            "--->Testcase Passed. "
            " user_type: %s, permission: %s, expect_read: %s,"
            " expect_write: %s.\n", user_type, permission, read, write)
