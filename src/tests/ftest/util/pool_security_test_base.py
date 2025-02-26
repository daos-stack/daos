"""
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import grp
import os
import random
import re

import agent_utils as agu
import security_test_base as secTestBase
from apricot import TestWithServers
from user_utils import groupadd, groupdel, useradd, userdel, usermod

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

    def modify_acl_file_entry(self, file_name, entry, new_entry):
        """Modify the acl_file acl list entry.

        Args:
            file_name (str): file name.
            entry (str): acl entry to be modified.
            new_entry (str): new acl entry.
        """
        with open(file_name, "r+") as acl_file:
            new_permissions = ""
            for line in acl_file.readlines():
                line = line.split("\n")[0]
                if line == entry:
                    line = new_entry
                    self.log.info("==>replacing \n %s  with\n %s", entry, new_entry)
                new_permissions = new_permissions + line + "\n"
            if entry is None:
                new_permissions = new_permissions + new_entry + "\n"
        with open(file_name, "w") as acl_file:
            acl_file.write(new_permissions)

    def get_pool_acl_list(self):
        """Get daos pool acl list by dmg get-acl.

        Returns:
            list: daos pool acl list.

        """
        result = self.pool.get_acl()

        pool_permission_list = []
        for line in result.stdout_text.splitlines():
            if line.startswith("A::"):
                found_user = re.search(r"A::(.+)@:(.*)", line)
                if found_user:
                    pool_permission_list.append(line)
            elif line.startswith("A:G:"):
                found_group = re.search(r"A:G:(.+)@:(.*)", line)
                if found_group:
                    pool_permission_list.append(line)
        return pool_permission_list

    def update_pool_acl_entry(self, action, entry):
        """Update daos pool acl list by dmg tool.

        Args:
            action (str): update-acl or delete-acl.
            entry (str): pool acl entry or principal to be updated.
        """
        if action == "delete":
            self.pool.delete_acl(entry)
        elif action == "update":
            self.pool.update_acl(use_acl=False, entry=entry)
        else:
            self.fail("##update_pool_acl_entry, action: {} is not supported."
                      "\n  supported action: update, delete.".format(action))

        self.log.info(
            " At update_pool_acl_entry, dmg.run result=\n %s",
            self.pool.dmg.result.stdout)

    @staticmethod
    def _command_failed(result):
        """Check whether the given command result had failed.

        Args:
            result (CmdResult or dict): dmg command output.
        """
        if isinstance(result, dict):
            # Result is JSON.
            return result["status"] != 0 or result["error"] is not None

        # Result is CmdResult.
        return result.exit_status != 0

    @staticmethod
    def _command_missing_err_code(result, err_code):
        """Check whether the given command result has given error code.

        Args:
            result (CmdResult or dict): dmg command output.
            err_code (str): Error code to look for in result.
        """
        if isinstance(result, dict):
            # Result is JSON.
            return err_code not in result["error"]

        # Result is CmdResult.
        return (err_code not in result.stderr_text
                and err_code not in result.stdout_text)

    def verify_daos_pool_cont_result(self, result, action, expect, err_code):
        """Verify the daos pool read or write action result.

        Args:
            result (CmdResult or dict): dmg command output.
            action (str): daos pool read or write.
            expect (str): expecting pass or deny.
            err_code (str): expecting deny of RC code.
        """
        if expect.lower() == 'pass':
            if self._command_failed(result):
                self.fail(
                    "##Test Fail on verify_daos_pool {}, expected Pass, but "
                    "Failed.".format(action))
            else:
                self.log.info(
                    " =Test Passed on verify_daos_pool %s, Succeed.\n", action)
        # Remove "and err_code not in result.stdout_text" on the next statement
        # elif after DAOS-5635 resolved.
        elif self._command_missing_err_code(result, err_code):
            self.fail(
                "##Test Fail on verify_daos_pool {}, expected Failure of {}, "
                "but Passed.".format(action, expect))
        else:
            self.log.info(
                " =Test Passed on verify_daos_pool %s expected error of %s.\n",
                action, expect)

    def verify_cont_rw_attribute(self, container, action, expect, attribute, value=None):
        """verify container rw attribute.

        Args:
            container (TestContainer): container to verify.
            action (str): daos pool read or write.
            expect (str): expecting pass or deny.
            attribute (str): Container attribute to be verified.
            value (str optional): Container attribute value to write.
        """
        if action.lower() == "write":
            with container.no_exception():
                result = container.set_attr(attrs={attribute: value})
        elif action.lower() == "read":
            with container.no_exception():
                result = container.get_attr(attr=attribute)
        else:
            result = None  # To appease pylint
            self.fail(
                "##In verify_cont_rw_attribute, "
                "invalid action: {}".format(action))
        self.log.info(
            "  In verify_cont_rw_attribute %s.\n =daos_cmd.run() result:\n%s",
            action, result)
        self.verify_daos_pool_cont_result(result, action, expect, DENY_ACCESS)

    def verify_cont_rw_property(self, container, action, expect, cont_property=None, value=None):
        """verify container rw property.

        Args:
            container (TestContainer): container to verify.
            action (str): daos container read or write.
            expect (str): expecting pass or deny.
            cont_property (str optional): Container property to be verified.
            value (str optional): Container property value to write.
        """
        if action.lower() == "write":
            with container.no_exception():
                result = container.set_prop(prop=cont_property, value=value)
        elif action.lower() == "read":
            with container.no_exception():
                result = container.get_prop()
        else:
            result = None  # To appease pylint
            self.fail("##In verify_cont_rw_property, invalid action: {}".format(action))
        self.log.info(
            "  In verify_cont_rw_property %s.\n =daos_cmd.run() result:\n%s", action, result)
        self.verify_daos_pool_cont_result(result, action, expect, DENY_ACCESS)

    def verify_cont_set_owner(self, container, expect, user, group):
        """verify container set owner.

        Args:
            container (TestContainer): container to verify.
            expect (str): expecting pass or deny.
            user (str): New user to be set.
            group (str): New group to be set.
        """
        action = "set"
        with container.no_exception():
            result = container.set_owner(user, group)
        self.log.info(
            "  In verify_cont_set_owner %s.\n =daos_cmd.run() result:\n%s", action, result)
        self.verify_daos_pool_cont_result(result, action, expect, DENY_ACCESS)

    def verify_cont_rw_acl(self, container, action, expect, entry=None):
        """verify container rw acl.

        Args:
            container (TestContainer): container to verify
            action (str): daos container read or write.
            expect (str): expecting pass or deny.
            entry (str optional): New ace entry to be write.
        """
        if action.lower() == "write":
            with container.no_exception():
                result = container.update_acl(entry=entry)
        elif action.lower() == "read":
            result = self.get_container_acl_list(container)
        else:
            result = None  # To appease pylint
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
                self.fail("##Test Fail on verify_cont_test_result, expected Pass, but Failed.")
            else:
                self.log.info(" =Test Passed on verify_cont_test_result Succeed.\n")
        elif DENY_ACCESS not in result:
            self.fail(
                "##Test Fail on verify_cont_test_result, expected Failure of -1001, but Passed.")
        else:
            self.log.info(
                " =Test Passed on verify_cont_test_result expected error of denial error -1001.\n")

    def verify_cont_delete(self, container, expect):
        """verify container delete.

        Args:
            container (TestContainer): container to verify.
            expect (str): expecting pass or deny.
        """
        action = "cont_delete"
        with container.daos.no_exception():
            result = container.daos.container_destroy(
                container.pool.identifier, container.identifier, True)
        self.log.info(
            "  In verify_cont_delete %s.\n =container.destroy() result:\n%s", action, result)
        self.verify_daos_pool_cont_result(result, action, expect, DENY_ACCESS)

    def setup_container_acl_and_permission(
            self, container, user_type, user_name, perm_type, perm_action):
        """Setup container acl and permissions.

        Args:
            container (TestContainer): container to setup.
            user_type (str): Container user_type.
            user_name (str): Container user_name.
            perm_type (str): Container permission type:
                             (attribute, property, acl, or ownership)
            perm_action (str): Container permission read/write action.
        """
        permission = "none"
        if perm_type == "attribute":
            permission = perm_action
        elif perm_type == "property":
            permission = perm_action.replace("r", "t")
            permission = permission.replace("w", "T")
        elif perm_type == "acl":
            permission = perm_action.replace("r", "a")
            permission = permission.replace("w", "A")
        elif perm_type == "ownership":
            permission = perm_action.replace("w", "to")
            permission = permission.replace("r", "rwdTAa")
        else:
            self.fail(
                "##In setup_container_acl_and_permission, unsupported "
                "perm_type {}".format(perm_type))
        self.log.info(
            "At setup_container_acl_and_permission, setup %s, %s, %s, with %s",
            user_type, user_name, perm_type, permission)
        with container.no_exception():
            result = container.update_acl(
                entry=secTestBase.acl_entry(user_type, user_name, permission))
        if result.stderr_text:
            self.fail(
                "##setup_container_acl_and_permission, fail on "
                "container.update_acl, expected Pass, but Failed.")

    def verify_pool_readwrite(self, pool, action, expect='Pass'):
        """Verify client is able to perform read or write on a pool.

        Args:
            pool (TestPool): pool to run verify in.
            action (str): read or write on pool.
            expect (str): expecting behavior pass or deny with RC -1001.

        Returns:
            bool: pass or fail.

        """
        deny_access = '-1001'
        daos_cmd = self.get_daos_command()
        with daos_cmd.no_exception():
            if action.lower() == "write":
                container = self.get_container(pool, create=False, daos=daos_cmd)
                result = container.create()
                container.skip_cleanup()
            elif action.lower() == "read":
                result = daos_cmd.pool_query(pool.identifier)
            else:
                result = None  # To appease pylint
                self.fail("##In verify_pool_readwrite, invalid action: {}".format(action))
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

        Returns:
            list: acl permission list on the acl_file.

        """
        user_prefix = self.params.get("user_prefix", "/run/pool_acl/*")
        user_list = []
        group_list = []
        for uid in range(num_user):
            username = user_prefix + "_tester_" + str(uid + 1)
            new_user = "A::" + username + "@:" + PERMISSIONS[uid % 4]
            if not useradd(self.log, self.hostlist_clients, username).passed:
                self.fail(f"Failed to useradd {username}")
            user_list.append(new_user)
        for gid in range(num_group):
            groupname = user_prefix + "_testGrp_" + str(gid + 1)
            new_group = "A:G:" + groupname + "@:" + PERMISSIONS[(gid + 2) % 4]
            if not groupadd(self.log, self.hostlist_clients, groupname).passed:
                self.fail(f"Failed to groupadd {groupname}")
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
        """
        user_prefix = self.params.get("user_prefix", "/run/pool_acl/*")
        for uid in range(num_user):
            username = user_prefix + "_tester_" + str(uid + 1)
            if not userdel(self.log, self.hostlist_clients, username).passed:
                self.log.error("Failed to userdel %s", username)
        for gid in range(num_group):
            groupname = user_prefix + "_testGrp_" + str(gid + 1)
            if not groupdel(self.log, self.hostlist_clients, groupname).passed:
                self.log.error("Failed to groupdel %s", groupname)

    def verify_pool_acl_prim_sec_groups(self, pool_acl_list, acl_file):
        """Verify daos pool acl access.

        Verify access with primary and secondary groups access permission.

        Args:
            pool_acl_list (list): pool acl entry list.
            acl_file (str): acl file to be used.
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
            if not groupadd(self.log, self.hostlist_clients, group).passed:
                self.fail(f"Failed to groupadd {group}")
        self.log.info("  (8-1)verify_pool_acl_prim_sec_groups, cmd=usermod")
        if not usermod(self.log, self.hostlist_clients, l_group, sec_group).passed:
            self.fail(f"Failed to usermod {l_group}")

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

        # dmg pool overwrite-acl <pool name> --acl-file <file>
        self.pool.overwrite_acl()
        self.log.info("  (8-4)dmg= %s", self.pool.dmg)
        self.log.info(
            "  (8-5)dmg.run() result=\n %s",
            self.pool.dmg.result.stdout)

        # Verify pool read operation
        # daos pool query <pool name>
        self.log.info("  (8-6)Verify pool read by: daos pool query --pool")
        exp_read = sec_group_rw[0]
        self.verify_pool_readwrite(self.pool, "read", expect=exp_read)

        # Verify pool write operation
        # daos container create <pool>
        self.log.info("  (8-7)Verify pool write by: daos container create pool")
        exp_write = sec_group_rw[1]
        self.verify_pool_readwrite(self.pool, "write", expect=exp_write)

        for group in sec_group:
            if not groupdel(self.log, self.hostlist_clients, group).passed:
                self.log.error("Failed to groupdel %s", group)

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
        self.pool = self.get_pool(connect=False, scm_size=scm_size, acl_file=acl_file)
        self.log.info("  (2)dmg= %s", self.pool.dmg)
        self.log.info("  (3)Create a pool with acl")

        # (4)Verify the pool create status
        self.log.info("  (4)dmg.run() result=\n%s", self.pool.dmg.result)
        if "ERR" in self.pool.dmg.result.stderr_text:
            self.fail("##(4)Unexpected error from pool create.")

        # (5)Get the pool's acl list
        #    dmg pool get-acl <pool name>
        self.log.info("  (5)Get a pool's acl list by: "
                      "dmg pool get-acl --pool --hostlist")
        pool_acl_list = self.get_pool_acl_list()
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
            self.update_pool_acl_entry("update", new_entry)
        for principal in acl_principals:
            self.update_pool_acl_entry("delete", principal)

        # (7)Verify pool read operation
        #    daos pool query <pool>
        self.log.info("  (7)Verify pool read by: daos pool query --pool")
        self.verify_pool_readwrite(self.pool, "read", expect=read)

        # (8)Verify pool write operation
        #    daos container create <pool>
        self.log.info("  (8)Verify pool write by: daos container create --pool")
        self.verify_pool_readwrite(self.pool, "write", expect=write)
        if secondary_grp_test:
            self.log.info("  (8-0)Verifying verify_pool_acl_prim_sec_groups")
            self.verify_pool_acl_prim_sec_groups(pool_acl_list, acl_file)

        # (9)Cleanup user and destroy pool
        self.log.info("  (9)Cleanup users and groups")
        self.cleanup_user_group(num_user, num_group)
        self.pool.destroy()
