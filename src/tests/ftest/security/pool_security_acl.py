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
from apricot import TestWithServers
from daos_utils import DaosCommand
from dmg_utils import DmgCommand
import dmg_utils
import random
import pwd
import grp
import re
from general_utils import pcmd


PERMISSIONS = ["", "r", "w", "rw"]


def acl_entry(usergroup, name, permission):
    '''
    Deascription:
        Create a daos acl entry on the specified user or groups
        with specified or random permission.
    Args:
        usergroup: user or group.
        name: user or group name to be created.
        permission: permission to be created.
    Return:
        entry: daos acl entry.
    '''
    if permission == "random":
        permission = random.choice(PERMISSIONS)
    if permission == "nonexist":
        return ""
    if "group" in usergroup:
        entry = "A:G:" + name + "@:" + permission
    else:
        entry = "A::" + name + "@:" + permission
    return entry


def add_del_user(hosts, ba_cmd, user):
    '''
    Deascription:
        Add or delete the daos user and group on host by sudo command.
    Args:
        hosts: list of host.
        ba_cmd: linux bash command to create user or group.
        user: user or group name to be created or cleaned.
    Return:
        none.
    '''
    bash_cmd = os.path.join("/usr/sbin", ba_cmd)
    homedir = ""
    if "user" in ba_cmd:
        homedir = "-r"
    cmd = " ".join(("sudo", bash_cmd, homedir, user))
    pcmd(hosts, cmd, False)


class PoolSecurityTest(TestWithServers):
    '''
    Test Class Description:
    Tests to verify the Pool security with dmg and daos tools on daos_server.
    :avocado: recursive
    '''

    def __init__(self, *args, **kwargs):
        """Initialize a Test object."""
        super(PoolSecurityTest, self).__init__(*args, **kwargs)
        self.dmg = None
        self.uuid = None

    def setUp(self):
        """Set up each test case."""
        super(PoolSecurityTest, self).setUp()

        # Create a DmgCommand object for issuing dmg commands
        self.dmg = DmgCommand(self.bin)
        self.dmg.set_hostlist(self.server_managers[0])

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.uuid is not None:
                # Destroy the pool created during the test
                self.dmg.sub_command_class.set_sub_command("destroy")
                self.dmg.sub_command_class.sub_command_class.pool.value = \
                    self.uuid
                self.dmg.run()
        finally:
            super(PoolSecurityTest, self).tearDown()

    def get_pool_acl_list(self, uuid):
        '''
        Deascription:
            Get daos pool acl list by dmg get-acl.
        Args:
            uuid: pool uuid number.
        Return:
            pool_permission_list: daos pool acl list.
        '''
        self.dmg.set_sub_command("pool")
        self.dmg.sub_command_class.set_sub_command("get-acl")
        self.dmg.sub_command_class.sub_command_class.pool.value = uuid
        result = self.dmg.run()

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

    def verify_daos_pool_result(self, result, action, expect, err_code):
        '''
        Deascription:
            To verify the daos pool read or write action result.
        Args:
            result: handle for daos pool action.
            action: daos pool read or write.
            expect: expecting pass or deny.
            err_code: expecting deny of RC code.
        Return:
            none.
        '''
        if expect.lower() == 'pass':
            if result.exit_status != 0 or result.stderr != "":
                self.fail(
                    "##Test Fail on verify_daos_pool {}, expected Pass, but "
                    "Failed.".format(action))
            else:
                self.log.info(" =Test Passed on verify_daos_pool %s, "
                              "Succeed.\n", action)
        elif err_code not in result.stderr:
            self.fail(
                "##Test Fail on verify_daos_pool {}, expected Failure of {}, "
                "but Passed.".format(action, expect))
        else:
            self.log.info(" =Test Passed on verify_daos_pool %s"
                          " expected error of %s.\n", action, expect)

    def verify_pool_readwrite(self, svc, uuid, action, expect='Pass'):
        '''
        Deascription:
            To verify client is able to perform read or write on a pool.
        Args:
            svc:  pool svc number.
            uuid: pool uuid number.
            action: read or write on pool.
            expect: expecting behavior pass or deny with RC -1001.
        Return:
            pass or fail.
        '''
        deny_access = '-1001'
        daos_cmd = DaosCommand(self.bin)
        daos_cmd.exit_status_exception = False

        if action.lower() == "write":
            daos_cmd.set_sub_command("container")
            daos_cmd.sub_command_class.set_sub_command("create")
        elif action.lower() == "read":
            daos_cmd.set_sub_command("pool")
            daos_cmd.sub_command_class.set_sub_command("query")
        else:
            self.fail(
                "##In verify_pool_readwrite, invalid action: {}".format(action))

        daos_cmd.sub_command_class.sub_command_class.pool.value = uuid
        daos_cmd.sub_command_class.sub_command_class.svc.value = svc
        daos_cmd.sub_command_class.sub_command_class.sys_name.value = \
            self.server_managers[0].get_config_value("name")
        result = daos_cmd.run()
        self.log.info("  In verify_pool_readwrite %s.\n =daos_cmd.run()"
                      " result:\n%s", action, result)
        self.verify_daos_pool_result(result, action, expect, deny_access)

    def create_pool_acl(self, num_user, num_group, current_user_acl, acl_file):
        '''
        Deascription:
            Create pool get-acl file with specified number of users and groups
            with random permission.
        Args:
            num_user:  number of user with random permission to be created.
            num_group: number of group with random permission to be created.
            current_user_acl: acl entries on current user to be created.
            acl_file: acl file to be created.
        Return:
            permission_list: acl permission list on the acl_file.
        '''
        user_prefix = self.params.get("user_prefix", "/run/pool_acl/*")
        user_list = []
        group_list = []
        for uid in range(num_user):
            username = user_prefix + "_tester_" + str(uid + 1)
            new_user = "A::" + username + "@:" + PERMISSIONS[uid % 4]
            add_del_user(self.hostlist_clients, "useradd", username)
            user_list.append(new_user)
        for gid in range(num_group):
            groupname = user_prefix + "_testGrp_" + str(gid + 1)
            new_group = "A:G:" + groupname + "@:" + PERMISSIONS[(gid + 2) % 4]
            add_del_user(self.hostlist_clients, "groupadd", groupname)
            group_list.append(new_group)
        permission_list = group_list + user_list + current_user_acl
        random.shuffle(permission_list)
        with open(acl_file, "w") as test_file:
            test_file.write("\n".join(permission_list))
        return permission_list

    def cleanup_user_group(self, num_user, num_group):
        '''
        Deascription:
            Remove the daos acl user and group on host.
        Args:
            num_user: number of user to be cleaned.
            num_group: number of group name to be cleaned.
        Return:
            none.
        '''
        user_prefix = self.params.get("user_prefix", "/run/pool_acl/*")
        for uid in range(num_user):
            username = user_prefix + "_tester_" + str(uid + 1)
            add_del_user(self.hostlist_clients, "userdel", username)
        for gid in range(num_group):
            groupname = user_prefix + "_testGrp_" + str(gid + 1)
            add_del_user(self.hostlist_clients, "groupdel", groupname)

    def pool_acl_verification(self, current_user_acl, read, write):
        '''
        Description:
            Daos pool security verification with acl file.
            Steps:
                (1)Setup dmg tool for creating a pool
                (2)Generate acl file with permissions
                (3)Create a pool with acl
                (4)Verify the pool create status
                (5)Get the pool's acl list
                (6)Verify pool read operation
                (7)Verify pool write operation
                (8)Cleanup user and destroy pool
        Args:
            current_user_acl: acl with read write access credential.
            read: expecting read permission.
            write: expecting write permission.
        Return:
            pass to continue.
            fail to report the testlog and stop.
        '''
        acl_file = os.path.join(
            self.tmp,
            self.params.get("acl_file", "/run/pool_acl/*", "acl_test.txt"))
        num_user = self.params.get("num_user", "/run/pool_acl/*")
        num_group = self.params.get("num_group", "/run/pool_acl/*")

        # (1)Create daos_shell command
        self.dmg.get_params(self)
        self.dmg.set_hostlist(self.server_managers[0])
        self.log.info("  (1)dmg= %s", str(self.dmg))

        # (2)Generate acl file with permissions
        self.log.info("  (2)Generate acl file with user/group permissions")
        permission_list = self.create_pool_acl(num_user,
                                               num_group,
                                               current_user_acl,
                                               acl_file)

        # (3)Create a pool with acl
        self.log.info("  (3)Create a pool with acl")
        self.dmg.sub_command_class.sub_command_class.acl_file.value = acl_file
        self.dmg.exit_status_exception = False
        result = self.dmg.run()

        # (4)Verify the pool create status
        self.log.info("  (4)dmg.run() result=\n%s", result)
        if result.stderr == "":
            self.uuid, svc = dmg_utils.get_pool_uuid_from_stdout(result.stdout)
        else:
            self.fail("##(4)Unable to parse pool uuid and svc.")

        # (5)Get the pool's acl list
        #    dmg pool get-acl --pool <UUID>
        self.log.info("  (5)Get a pool's acl list by: "
                      "dmg pool get-acl --pool --hostlist")
        pool_acl_list = self.get_pool_acl_list(self.uuid)
        self.log.info(
            "   pool original permission_list: %s", permission_list)
        self.log.info(
            "   pool get_acl  permission_list: %s", pool_acl_list)

        # (6)Verify pool read operation
        #    daos pool query --pool <uuid>
        self.log.info("  (6)Verify pool read by: daos pool query --pool")
        self.verify_pool_readwrite(svc, self.uuid, "read", expect=read)

        # (7)Verify pool write operation
        #    daos continer create --pool <uuid>
        self.log.info("  (7)Verify pool write by: daos continer create --pool")
        self.verify_pool_readwrite(svc, self.uuid, "write", expect=write)

        # (8)Cleanup user and destroy pool
        self.log.info("  (8)Cleanup user and destroy pool")
        self.cleanup_user_group(num_user, num_group)

    def test_pool_acl_enforcement(self):
        '''
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
        Description:
            Create pool with pass-in user and group acl permission,
            verify pool user and group read, write, read-write and none
            permissions enforcement with all forms of input under different
            test sceanrios.
        :avocado: tags=all,full_regression,security,pool_acl,sec_acl
        '''
        user_uid = os.geteuid()
        user_gid = os.getegid()
        current_user = pwd.getpwuid(user_uid)[0]
        current_group = grp.getgrgid(user_gid)[0]
        user_type = self.params.get("user", "/run/pool_acl/*")[0].lower()
        permission, read, write = self.params.get("name", "/run/pool_acl/*")

        user_types = ["owner", "user", "ownergroup", "group", "everyone"]
        default_acl_entries = ["A::OWNER@:",
                               acl_entry("user", current_user, ""),
                               "A:G:GROUP@:",
                               acl_entry("group", current_group, ""),
                               "A::EVERYONE@:"]
        test_acl_entries = ["", "", "", "", ""]

        if permission.lower() == "none":
            permission = ""
        if permission not in PERMISSIONS:
            self.fail(
                "##permission {} is invalid, valid permissions are: 'none', "
                "'r', w', 'rw'".format(permission))
        if user_type not in user_types:
            self.fail(
                "##user_type {} is invalid, valid user_types are: {}".format(
                    user_type, user_types))
        user_type_ind = user_types.index(user_type)
        self.log.info("===>Start DAOS pool acl enforcement order Testcase: "
                      " user_type: %s, permission: %s, expect_read: %s,"
                      " expect_write: "
                      "%s.", user_type, permission, read, write)
        # take care of the user_type which have higher priviledge
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
            # "A::OWNER@:" + permission
            test_acl_entries[ind] = default_acl_entries[ind] + test_permission
        # union of ownergroup and group permission
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
