#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from __future__ import print_function

import os
import random
import grp
import re
from apricot import TestWithServers
from daos_utils import DaosCommand
import agent_utils as agu
import security_test_base as secTestBase

PERMISSIONS = ["", "r", "w", "rw"]
DENY_ACCESS = "-1001"

class PoolSecurityTestBase(TestWithServers):
    # pylint: disable=no-member

    """Pool security test cases.

    Test Class Description:
        Test methods to verify the Pool security with acl by dmg
        and daos tools.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a PoolSecurityTestBase object."""
        super(PoolSecurityTestBase, self).__init__(*args, **kwargs)
        self.dmg = None

    def setUp(self):
        """Set up each test case."""
        super(PoolSecurityTestBase, self).setUp()

        # Setup the dmg command object - requires a server to be started
        self.dmg = self.get_dmg_command()

    def modify_acl_file_entry(self, file_name, entry, new_entry):
        """Modify the acl_file acl list entry.

        Args:
            file_name (str): file name.
            entry (str): acl entry to be modified.
            new_entry (str): new acl entry.

        Return:
            none.

        """
        acl_file = open(file_name, "r+")
        new_permissions = ""
        for line in acl_file.readlines():
            line = line.split("\n")[0]
            if line == entry:
                line = new_entry
                self.log.info(
                    "==>replaceing \n %s  with\n %s", entry, new_entry)
            new_permissions = new_permissions + line + "\n"
        if entry is None:
            new_permissions = new_permissions + new_entry + "\n"
        acl_file.close()
        acl_file = open(file_name, "w")
        acl_file.write(new_permissions)
        acl_file.close()

    def get_pool_acl_list(self, uuid):
        """Get daos pool acl list by dmg get-acl.

        Args:
            uuid (str): pool uuid.

        Return:
            list: daos pool acl list.

        """
        result = self.dmg.pool_get_acl(uuid)

        pool_permission_list = []
        for line in result.stdout.splitlines():
            if not line.startswith("A:"):
                continue
            elif line.startswith("A::"):
                found_user = re.search(r"A::(.+)@:(.*)", line)
                if found_user:
                    pool_permission_list.append(line)
            elif line.startswith("A:G:"):
                found_group = re.search(r"A:G:(.+)@:(.*)", line)
                if found_group:
                    pool_permission_list.append(line)
        return pool_permission_list

    def update_pool_acl_entry(self, uuid, action, entry):
        """Update daos pool acl list by dmg tool.

        Args:
            uuid (str): pool uuid.
            action (str): update-acl or delete-acl.
            entry (str): pool acl entry or principal to be updated.

        Return:
            none.

        """
        if action == "delete":
            result = self.dmg.pool_delete_acl(uuid, entry)
        elif action == "update":
            result = self.dmg.pool_update_acl(uuid, None, entry)
        else:
            self.fail("##update_pool_acl_entry, action: {} is not supported."
                      "\n  supported action: update, delete.".format(action))
        self.log.info(" At update_pool_acl_entry, dmg.run result=\n %s", result)

    def verify_daos_pool_cont_result(self, result, action, expect, err_code):
        """Verify the daos pool read or write action result.

        Args:
            result (CmdResult): handle for daos pool action.
            action (str): daos pool read or write.
            expect (str): expecting pass or deny.
            err_code (str): expecting deny of RC code.

        Return:
            none.

        """
        if expect.lower() == 'pass':
            if result.exit_status != 0 or result.stderr != "":
                self.fail(
                    "##Test Fail on verify_daos_pool {}, expected Pass, but "
                    "Failed.".format(action))
            else:
                self.log.info(
                    " =Test Passed on verify_daos_pool %s, Succeed.\n", action)
        # Remove "and err_code not in result.stdout" on the next statement elif
        # after DAOS-5635 resolved.
        elif err_code not in result.stderr and err_code not in result.stdout:
            self.fail(
                "##Test Fail on verify_daos_pool {}, expected Failure of {}, "
                "but Passed.".format(action, expect))
        else:
            self.log.info(
                " =Test Passed on verify_daos_pool %s expected error of %s.\n",
                action, expect)

    def verify_cont_rw_attribute(self, action, expect, attribute, value=None):
        """verify container rw attribute.

        Args:
            action (str): daos pool read or write.
            expect (str): expecting pass or deny.
            attribute (str): Container attribute to be verified.
            value (str optional): Container attribute value to write.

        """
        if action.lower() == "write":
            result = self.set_container_attribute(self.pool_uuid,
                                                  self.container_uuid,
                                                  attribute,
                                                  value)
        elif action.lower() == "read":
            result = self.get_container_attribute(self.pool_uuid,
                                                  self.container_uuid,
                                                  attribute)
        else:
            self.fail(
                "##In verify_cont_rw_attribute, "
                "invalid action: {}".format(action))
        self.log.info(
            "  In verify_cont_rw_attribute %s.\n =daos_cmd.run() result:\n%s",
            action, result)
        self.verify_daos_pool_cont_result(result, action, expect, DENY_ACCESS)

    def verify_cont_rw_property(
            self, action, expect, cont_property=None, value=None):
        """verify container rw property.

        Args:
            action (str): daos container read or write.
            expect (str): expecting pass or deny.
            cont_property (str optional): Container property to be verified.
            value (str optional): Container property value to write.

        """
        if action.lower() == "write":
            result = self.set_container_property(self.pool_uuid,
                                                 self.container_uuid,
                                                 cont_property,
                                                 value)
        elif action.lower() == "read":
            result = self.get_container_property(self.pool_uuid,
                                                 self.container_uuid)
        else:
            self.fail(
                "##In verify_cont_rw_property, "
                "invalid action: {}".format(action))
        self.log.info(
            "  In verify_cont_rw_property %s.\n =daos_cmd.run() result:\n%s",
            action, result)
        self.verify_daos_pool_cont_result(result, action, expect, DENY_ACCESS)

    def verify_cont_set_owner(self, expect, user, group):
        """verify container set owner.

        Args:
            expect (str): expecting pass or deny.
            user (str): New user to be set.
            group (str): New group to be set.

        """
        action = "set"
        result = self.set_container_owner(self.pool_uuid,
                                          self.container_uuid,
                                          user,
                                          group)
        self.log.info(
            "  In verify_cont_set_owner %s.\n =daos_cmd.run() result:\n%s",
            action, result)
        self.verify_daos_pool_cont_result(result, action, expect, DENY_ACCESS)

    def verify_cont_rw_acl(self, action, expect, entry=None):
        """verify container rw acl.

        Args:
            action (str): daos container read or write.
            expect (str): expecting pass or deny.
            entry (str optional): New ace entry to be write.

        """
        if action.lower() == "write":
            result = self.update_container_acl(entry)
        elif action.lower() == "read":
            result = self.get_container_acl_list(self.pool_uuid,
                                                 self.container_uuid)
        else:
            self.fail(
                "##In verify_cont_rw_acl, invalid action: {}".format(action))
        self.log.info(
            "  In verify_cont_rw_acl %s.\n =daos_cmd.run() result:\n%s",
            action, result)
        if action == "read":
            self.verify_cont_test_result(result, expect)
        else:
            self.verify_daos_pool_cont_result(
                result, action, expect, DENY_ACCESS)

    def verify_cont_test_result(self, result, expect):
        """verify container acl test result and report pass or fail.

        Args:
            result (str): daos container cmd result to be verified.
            expect (str): expecting pass or deny.

        """
        if expect.lower() == 'pass':
            if DENY_ACCESS in result:
                self.fail(
                    "##Test Fail on verify_cont_test_result, expected Pass, "
                    "but Failed.")
            else:
                self.log.info(
                    " =Test Passed on verify_cont_test_result Succeed.\n")
        elif DENY_ACCESS not in result:
            self.fail(
                "##Test Fail on verify_cont_test_result, expected Failure of "
                "-1001, but Passed.")
        else:
            self.log.info(
                " =Test Passed on verify_cont_test_result expected error of "
                "denial error -1001.\n")

    def verify_cont_delete(self, expect):
        """verify container delete.

        Args:
            expect (str): expecting pass or deny.

        """
        action = "cont_delete"
        result = self.test_container_destroy(
            self.pool_uuid, self.container)
        self.log.info(
            "  In verify_cont_delete %s.\n =test_container_destroy() result:"
            "\n%s", action, result)
        self.verify_daos_pool_cont_result(result, action, expect, DENY_ACCESS)

    def setup_container_acl_and_permission(
            self, user_type, user_name, perm_type, perm_action):
        """Setup container acl and permissions.

        Args:
            user_type (str): Container user_type.
            user_name (str): Container user_name.
            perm_type (str): Container permission type:
                             (attribute, property, acl, or ownership)
            perm_action (str): Container permission read/write action.

        """
        permission = "none"
        if perm_type is "attribute":
            permission = perm_action
        elif perm_type is "property":
            permission = perm_action.replace("r", "t")
            permission = permission.replace("w", "T")
        elif perm_type is "acl":
            permission = perm_action.replace("r", "a")
            permission = permission.replace("w", "A")
        elif perm_type is "ownership":
            permission = perm_action.replace("w", "to")
            permission = permission.replace("r", "rwdTAa")
        else:
            self.fail(
                "##In setup_container_acl_and_permission, unsupported "
                "perm_type %s", perm_type)
        self.log.info(
            "At setup_container_acl_and_permission, setup %s, %s, %s, with %s",
            user_type, user_name, perm_type, permission)
        result = self.update_container_acl(
            secTestBase.acl_entry(user_type, user_name, permission))
        if result.stderr is not "":
            self.fail(
                "##setup_container_acl_and_permission, fail on "
                "update_container_acl, expected Pass, but Failed.")

    def verify_pool_readwrite(self, uuid, action, expect='Pass'):
        """Verify client is able to perform read or write on a pool.

        Args:
            uuid (str): pool uuid number.
            action (str): read or write on pool.
            expect (str): expecting behavior pass or deny with RC -1001.

        Return:
            bool: pass or fail.

        """
        deny_access = '-1001'
        daos_cmd = DaosCommand(self.bin)
        daos_cmd.exit_status_exception = False
        if action.lower() == "write":
            result = daos_cmd.container_create(pool=uuid)
        elif action.lower() == "read":
            result = daos_cmd.pool_query(pool=uuid)
        else:
            self.fail(
                "##In verify_pool_readwrite, invalid action: {}".format(action))
        self.log.info(
            "  In verify_pool_readwrite %s.\n =daos_cmd.run() result:\n%s",
            action, result)
        self.verify_daos_pool_cont_result(result, action, expect, deny_access)

    def create_pool_acl(self, num_user, num_group, current_user_acl, acl_file):
        """Create pool get-acl file.

        Create the pool acl file with specified number of users and groups with
        random permission.

        Args:
            num_user (int):  number of users to be created.
            num_group (int): number of groups to be created.
            current_user_acl (list): acl entries on current user to be created.
            acl_file (str): acl file to be created.

        Return:
            list: acl permission list on the acl_file.

        """
        user_prefix = self.params.get("user_prefix", "/run/pool_acl/*")
        user_list = []
        group_list = []
        for uid in range(num_user):
            username = user_prefix + "_tester_" + str(uid + 1)
            new_user = "A::" + username + "@:" + PERMISSIONS[uid % 4]
            secTestBase.add_del_user(self.hostlist_clients, "useradd", username)
            user_list.append(new_user)
        for gid in range(num_group):
            groupname = user_prefix + "_testGrp_" + str(gid + 1)
            new_group = "A:G:" + groupname + "@:" + PERMISSIONS[(gid + 2) % 4]
            secTestBase.add_del_user(self.hostlist_clients, "groupadd",
                                     groupname)
            group_list.append(new_group)
        permission_list = group_list + user_list + current_user_acl
        random.shuffle(permission_list)
        with open(acl_file, "w") as test_file:
            test_file.write("\n".join(permission_list))
        return permission_list

    def cleanup_user_group(self, num_user, num_group):
        """Remove the daos acl user and group on host.

        Args:
            num_user (int): number of user to be cleaned.
            num_group (int): number of group name to be cleaned.

        Return:
            none.

        """
        user_prefix = self.params.get("user_prefix", "/run/pool_acl/*")
        for uid in range(num_user):
            username = user_prefix + "_tester_" + str(uid + 1)
            secTestBase.add_del_user(self.hostlist_clients, "userdel", username)
        for gid in range(num_group):
            groupname = user_prefix + "_testGrp_" + str(gid + 1)
            secTestBase.add_del_user(self.hostlist_clients, "groupdel",
                                     groupname)

    def verify_pool_acl_prim_sec_groups(self, pool_acl_list, acl_file, uuid):
        """Verify daos pool acl access.

        Verify access with primary and secondary groups access permission.

        Args:
            pool_acl_list (list): pool acl entry list.
            acl_file (str): acl file to be used.
            uuid (str): daos pool uuid.

        Return:
            None.

        """
        sec_group = self.params.get("secondary_group_name", "/run/pool_acl/*")
        sec_group_perm = self.params.get("sg_permission", "/run/pool_acl/*")
        sec_group_rw = self.params.get("sg_read_write", "/run/pool_acl/*")
        user_gid = os.getegid()
        current_group = grp.getgrgid(user_gid)[0]
        primary_grp_perm = self.params.get(
            "pg_permission", "/run/pool_acl/primary_secondary_group_test/*")[0]
        sec_group = self.params.get(
            "secondary_group_name",
            "/run/pool_acl/primary_secondary_group_test/*")
        sec_group_perm = self.params.get(
            "sg_permission", "/run/pool_acl/primary_secondary_group_test/*")
        sec_group_rw = self.params.get(
            "sg_read_write", "/run/pool_acl/primary_secondary_group_test/*")
        l_group = grp.getgrgid(os.getegid())[0]
        for group in sec_group:
            secTestBase.add_del_user(self.hostlist_clients, "groupadd", group)
        cmd = "usermod -G " + ",".join(sec_group)
        self.log.info("  (8-1)verify_pool_acl_prim_sec_groups, cmd= %s", cmd)
        secTestBase.add_del_user(self.hostlist_clients, cmd, l_group)

        self.log.info(
            "  (8-2)Before update sec_group permission, pool_acl_list= %s",
            pool_acl_list)
        for group, permission in zip(sec_group, sec_group_perm):
            if permission == "none":
                permission = ""
            n_acl = secTestBase.acl_entry("group", group, permission,
                                          PERMISSIONS)
            pool_acl_list.append(n_acl)

        self.log.info(
            "  (8-3)After update sec_group permission, pool_acl_list= %s",
            pool_acl_list)
        self.log.info("      pool acl_file= %s", acl_file)
        secTestBase.create_acl_file(acl_file, pool_acl_list)

        # modify primary-group permission for secondary-group test
        grp_entry = secTestBase.acl_entry("group", current_group,
                                          primary_grp_perm, PERMISSIONS)
        new_grp_entry = secTestBase.acl_entry("group", current_group, "",
                                              PERMISSIONS)
        self.modify_acl_file_entry(acl_file, grp_entry, new_grp_entry)

        # dmg pool overwrite-acl --pool <uuid> --acl-file <file>
        result = self.dmg.pool_overwrite_acl(uuid, acl_file)
        self.log.info("  (8-4)dmg= %s", self.dmg)
        self.log.info("  (8-5)dmg.run() result=\n %s", result)

        # Verify pool read operation
        # daos pool query --pool <uuid>
        self.log.info("  (8-6)Verify pool read by: daos pool query --pool")
        exp_read = sec_group_rw[0]
        self.verify_pool_readwrite(uuid, "read", expect=exp_read)

        # Verify pool write operation
        # daos container create --pool <uuid>
        self.log.info("  (8-7)Verify pool write by: daos container create pool")
        exp_write = sec_group_rw[1]
        self.verify_pool_readwrite(uuid, "write", expect=exp_write)

        for group in sec_group:
            secTestBase.add_del_user(self.hostlist_clients, "groupdel", group)

    def pool_acl_verification(self, current_user_acl, read, write,
                              secondary_grp_test=False):
        """Verify the daos pool security with an acl file.

        Steps:
            (1)Setup dmg tool for creating a pool
            (2)Generate acl file with permissions
            (3)Create a pool with acl
            (4)Verify the pool create status
            (5)Get the pool's acl list
            (6)Verify pool's ace entry update and delete
            (7)Verify pool read operation
            (8)Verify pool write operation
            (9)Cleanup user and destroy pool

        Args:
            current_user_acl (str): acl with read write access credential.
            read (str): expecting read permission.
            write (str): expecting write permission.

        Return:
            None: pass to continue; fail to report the testlog and stop.

        """
        # (1)Create dmg command
        scm_size = self.params.get("scm_size", "/run/pool_acl/*")
        get_acl_file = self.params.get(
            "acl_file", "/run/pool_acl/*", "acl_test.txt")
        acl_file = os.path.join(self.tmp, get_acl_file)
        num_user = self.params.get("num_user", "/run/pool_acl/*")
        num_group = self.params.get("num_group", "/run/pool_acl/*")
        self.hostlist_clients = agu.include_local_host(self.hostlist_clients)

        # (2)Generate acl file with permissions
        self.log.info("  (1)Generate acl file with user/group permissions")
        permission_list = self.create_pool_acl(num_user,
                                               num_group,
                                               current_user_acl,
                                               acl_file)

        # (3)Create a pool with acl
        self.dmg.exit_status_exception = False
        data = self.dmg.pool_create(scm_size, acl_file=acl_file)
        self.dmg.exit_status_exception = True
        self.log.info("  (2)dmg= %s", self.dmg)
        self.log.info("  (3)Create a pool with acl")

        # (4)Verify the pool create status
        self.log.info("  (4)dmg.run() result=\n%s", self.dmg.result)
        if "ERR" in self.dmg.result.stderr:
            self.fail("##(4)Unable to parse pool uuid and svc.")

        # (5)Get the pool's acl list
        #    dmg pool get-acl --pool <UUID>
        self.log.info("  (5)Get a pool's acl list by: "
                      "dmg pool get-acl --pool --hostlist")
        pool_acl_list = self.get_pool_acl_list(data["uuid"])
        self.log.info(
            "   pool original permission_list: %s", permission_list)
        self.log.info(
            "   pool get_acl  permission_list: %s", pool_acl_list)

        # (6)Verify pool acl ace update and delete
        self.log.info("  (6)Verify update and delete of pool's acl entry.")
        tmp_ace = "daos_ci_test_new"
        new_entries = [secTestBase.acl_entry("user", tmp_ace, "", PERMISSIONS),
                       secTestBase.acl_entry("group", tmp_ace, "", PERMISSIONS)]
        acl_principals = [secTestBase.acl_principal("user", tmp_ace),
                          secTestBase.acl_principal("group", tmp_ace)]
        for new_entry in new_entries:
            self.update_pool_acl_entry(data["uuid"], "update", new_entry)
        for principal in acl_principals:
            self.update_pool_acl_entry(data["uuid"], "delete", principal)

        # (7)Verify pool read operation
        #    daos pool query --pool <uuid>
        self.log.info("  (7)Verify pool read by: daos pool query --pool")
        self.verify_pool_readwrite(
            data["uuid"], "read", expect=read)

        # (8)Verify pool write operation
        #    daos container create --pool <uuid>
        self.log.info("  (8)Verify pool write by: daos container create --pool")
        self.verify_pool_readwrite(
            data["uuid"], "write", expect=write)
        if secondary_grp_test:
            self.log.info("  (8-0)Verifying verify_pool_acl_prim_sec_groups")
            self.verify_pool_acl_prim_sec_groups(
                pool_acl_list, acl_file, data["uuid"])

        # (9)Cleanup user and destroy pool
        self.log.info("  (9)Cleanup users and groups")
        self.cleanup_user_group(num_user, num_group)
