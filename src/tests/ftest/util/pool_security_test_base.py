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
from apricot import TestWithServers
from daos_utils import DaosCommand
from dmg_utils import DmgCommand
import dmg_utils
import random
import grp
import re
from general_utils import pcmd

PERMISSIONS = ["", "r", "w", "rw"]

def acl_entry(usergroup, name, permission):
    '''
    Deascription:
        Create a daos acl entry for the specified user or group.
        with specified or random permission.
    Args:
        usergroup: user or group.
        name: user or group name to be created.
        permission: permission to be created.
    Return:
        entry: daos pool acl entry.
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

def acl_principal(usergroup, name):
    '''
    Deascription:
        Create a daos ace principal for the specified user or group.
    Args:
        usergroup: user or group.
        name: user or group name to be created.
    Return:
        entry: daos pool acl entry.
    '''
    if "group" in usergroup:
        entry = "g:" + name + "@"
    else:
        entry = "u:" + name + "@"
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
    if "usermod" not in ba_cmd and "user" in ba_cmd:
        homedir = "-r"
    cmd = " ".join(("sudo", bash_cmd, homedir, user))
    print("     =Clients/hosts {0}, exec cmd: {1}".format(hosts, cmd))
    pcmd(hosts, cmd, False)

def create_acl_file(file_name, permissions):
    '''
    Deascription:
        Create a acl_file with permissions.
    Args:
        file_name: file name.
        permissions: daos acl permission list.
    Return:
        none.
    '''

    acl_file = open(file_name, "w")
    acl_file.write("\n".join(permissions))
    acl_file.close()

class PoolSecurityTestBase(TestWithServers):
    '''
    Test Class Description:
        Test methods to verify the Pool security with acl by dmg
        and daos tools.
    :avocado: recursive
    '''

    def modify_acl_file_entry(self, file_name, entry, new_entry):
        '''
        Deascription:
            Modify the acl_file acl list entry.
        Args:
            file_name: file name.
            entry: acl entry to be modified.
            new_entry: new acl entry.
        Return:
            none.
        '''
        acl_file = open(file_name, "r+")
        new_permissions = ""
        for line in acl_file.readlines():
            line = line.split("\n")[0]
            if line == entry:
                line = new_entry
                self.log.info("==>replaceing \n %s  with\n %s",\
                    entry, new_entry)
            new_permissions = new_permissions + line + "\n"
        if entry is None:
            new_permissions = new_permissions + new_entry + "\n"
        acl_file.close()
        acl_file = open(file_name, "w")
        acl_file.write(new_permissions)
        acl_file.close()

    def get_pool_acl_list(self, uuid):
        '''
        Deascription:
            Get daos pool acl list by dmg get-acl.
        Args:
            uuid: pool uuid.
        Return:
            pool_permission_list: daos pool acl list.
        '''
        dmg = DmgCommand(os.path.join(self.prefix, "bin"))
        dmg.request.value = "pool"
        dmg.action.value = "get-acl --pool " + uuid
        port = self.params.get("port", "/run/server_config/*")
        servers_with_ports = [
            "{}:{}".format(host, port) for host in self.hostlist_servers]
        dmg.hostlist.update(",".join(servers_with_ports), "dmg.hostlist")
        result = dmg.run()

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
        '''
        Deascription:
            Update daos pool acl list by dmg tool.
        Args:
            uuid: pool uuid.
            action: update-acl or delete-acl.
            entry: pool acl entry or principal to be updated.
        Return:
            none.
        '''
        dmg = DmgCommand(os.path.join(self.prefix, "bin"))
        dmg.request.value = "pool"
        if action is "delete":
            dmg.action.value = "delete-acl --pool " + uuid
            dmg.action.value += " --principal " + entry
        elif action is "update":
            dmg.action.value = "update-acl --pool " + uuid
            dmg.action.value += " --entry " + entry
        else:
            self.fail("##update_pool_acl_entry, action: {} is not supported."
                      "\n  supported action: update, delete.".format(action))
        port = self.params.get("port", "/run/server_config/*")
        servers_with_ports = [
            "{}:{}".format(host, port) for host in self.hostlist_servers]
        dmg.hostlist.update(",".join(servers_with_ports), "dmg.hostlist")
        result = dmg.run()
        self.log.info(" At update_pool_acl_entry, dmg.run result=\n %s",\
            result)

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
                self.fail("##Test Fail on verify_daos_pool {}, "
                          "expected Pass, but Failed.".format(action))
            else:
                self.log.info(" =Test Passed on verify_daos_pool %s, "
                              "Succeed.\n", action)
        elif err_code not in result.stderr:
            self.fail("##Test Fail on verify_daos_pool {},"
                      " expected Failure of {}, but Passed."\
                      .format(action, expect))
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
        daos_cmd = DaosCommand(os.path.join(self.prefix, "bin"))
        if action.lower() == "write":
            daos_cmd.request.value = "container"
            daos_cmd.action.value = "create --svc={} --pool={}".format(
                svc, uuid)
        elif action.lower() == "read":
            daos_cmd.request.value = "pool"
            daos_cmd.action.value = "query --svc={} --pool={}".format(
                svc, uuid)
        else:
            self.fail("##In verify_pool_readwrite, invalid action: "
                      "%s", action)
        daos_cmd.exit_status_exception = False
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

    def verify_pool_acl_prim_sec_groups(self, pool_acl_list, acl_file,\
        uuid, svc):
        '''
        Deascription:
            Verify daos pool acl access with primary and secondary
            groups access permission.
        Args:
            pool_acl_list: pool acl entry list.
            acl_file: acl file to be used.
            uuid: daos pool uuid.
            svc:  daos pool svc.
        Return:
            None.
        '''
        sec_group = self.params.get("secondary_group_name", "/run/pool_acl/*")
        sec_group_perm = self.params.get("sg_permission", "/run/pool_acl/*")
        sec_group_rw = self.params.get("sg_read_write", "/run/pool_acl/*")
        user_gid = os.getegid()
        current_group = grp.getgrgid(user_gid)[0]
        primary_grp_perm = self.params.get(\
            "pg_permission", "/run/pool_acl/primary_secondary_group_test/*")[0]
        sec_group = self.params.get(\
            "secondary_group_name", \
            "/run/pool_acl/primary_secondary_group_test/*")
        sec_group_perm = self.params.get(\
            "sg_permission", "/run/pool_acl/primary_secondary_group_test/*")
        sec_group_rw = self.params.get(\
            "sg_read_write", "/run/pool_acl/primary_secondary_group_test/*")
        l_group = grp.getgrgid(os.getegid())[0]
        for group in sec_group:
            add_del_user(self.hostlist_clients, "groupadd", group)
        cmd = "usermod -G " + ",".join(sec_group)
        self.log.info("  (8-1)verify_pool_acl_prim_sec_groups, cmd= %s", cmd)
        add_del_user(self.hostlist_clients, cmd, l_group)

        self.log.info("  (8-2)Before update sec_group permission,\
            pool_acl_list= %s", pool_acl_list)
        for group, permission in zip(sec_group, sec_group_perm):
            if permission == "none":
                permission = ""
            n_acl = acl_entry("group", group, permission)
            pool_acl_list.append(n_acl)

        self.log.info("  (8-3)After update sec_group permission,\
            pool_acl_list= %s", pool_acl_list)
        self.log.info("      pool acl_file= %s", acl_file)
        create_acl_file(acl_file, pool_acl_list)

        #modify primary-group permission for secondary-group test
        grp_entry = acl_entry("group", current_group, primary_grp_perm)
        new_grp_entry = acl_entry("group", current_group, "")
        self.modify_acl_file_entry(acl_file, grp_entry, new_grp_entry)

        #dmg pool overwrite-acl --pool <uuid> --acl-file <file>
        dmg = DmgCommand(os.path.join(self.prefix, "bin"))
        dmg.request.value = "pool"
        dmg.action.value = "overwrite-acl --pool={} --acl-file={}".\
            format(uuid, acl_file)
        port = self.params.get("port", "/run/server_config/*", 10001)
        servers_with_ports = [
            "{}:{}".format(host, port) for host in self.hostlist_servers]
        dmg.hostlist.update(",".join(servers_with_ports), "dmg.hostlist")
        self.log.info("  (8-4)dmg= %s", dmg)
        result = dmg.run()
        self.log.info("  (8-5)dmg.run() result=\n %s", result)

        #Verify pool read operation
        #daos pool query --pool <uuid>
        self.log.info("  (8-6)Verify pool read by: daos pool query --pool")
        exp_read = sec_group_rw[0]
        self.verify_pool_readwrite(svc, uuid, "read", expect=exp_read)

        #Verify pool write operation
        #daos continer create --pool <uuid>
        self.log.info("  (8-7)Verify pool write by: daos continer create pool")
        exp_write = sec_group_rw[1]
        self.verify_pool_readwrite(svc, uuid, "write", expect=exp_write)

        for group in sec_group:
            add_del_user(self.hostlist_clients, "groupdel", group)

    def pool_acl_verification(self, current_user_acl, read, write,\
        secondary_grp_test=False):
        '''
        Deascription:
            Daos pool security verification with acl file.
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
            current_user_acl: acl with read write access credential.
            read: expecting read permission.
            write: expecting write permission.
        Return:
            pass to continue.
            fail to report the testlog and stop.
        '''
        # (1)Create dmg command
        dmg = DmgCommand(os.path.join(self.prefix, "bin"))
        dmg.get_params(self)
        port = self.params.get("port", "/run/server_config/*", 10001)
        get_acl_file = self.params.get("acl_file", "/run/pool_acl/*",
                                       "acl_test.txt")
        acl_file = os.path.join(self.tmp, get_acl_file)
        num_user = self.params.get("num_user", "/run/pool_acl/*")
        num_group = self.params.get("num_group", "/run/pool_acl/*")
        servers_with_ports = [
            "{}:{}".format(host, port) for host in self.hostlist_servers]
        dmg.hostlist.update(",".join(servers_with_ports), "dmg.hostlist")
        self.log.info("  (1)dmg= %s", dmg)

        # (2)Generate acl file with permissions
        self.log.info("  (2)Generate acl file with user/group permissions")
        permission_list = self.create_pool_acl(num_user,
                                               num_group,
                                               current_user_acl,
                                               acl_file)

        # (3)Create a pool with acl
        self.log.info("  (3)Create a pool with acl")
        dmg.action_command.acl_file.value = acl_file
        dmg.exit_status_exception = False
        result = dmg.run()

        # (4)Verify the pool create status
        self.log.info("  (4)dmg.run() result=\n%s", result)
        if "ERR" not in result.stderr:
            uuid, svc = dmg_utils.get_pool_uuid_service_replicas_from_stdout\
                (result.stdout)
        else:
            self.fail("##(4)Unable to parse pool uuid and svc.")

        # (5)Get the pool's acl list
        #    dmg pool get-acl --pool <UUID>
        self.log.info("  (5)Get a pool's acl list by: "
                      "dmg pool get-acl --pool --hostlist")
        pool_acl_list = self.get_pool_acl_list(uuid)
        self.log.info(
            "   pool original permission_list: %s", permission_list)
        self.log.info(
            "   pool get_acl  permission_list: %s", pool_acl_list)

        # (6)Verify pool acl ace update and delete
        self.log.info("  (6)Verify update and delete of pool's acl entry.")
        tmp_ace = "daos_ci_test_new"
        new_entrys = [acl_entry("user", tmp_ace, ""),
                      acl_entry("group", tmp_ace, "")]
        acl_principals = [acl_principal("user", tmp_ace),
                          acl_principal("group", tmp_ace)]
        for new_entry in new_entrys:
            self.update_pool_acl_entry(uuid, "update", new_entry)
        for principal in acl_principals:
            self.update_pool_acl_entry(uuid, "delete", principal)

        # (7)Verify pool read operation
        #    daos pool query --pool <uuid>
        self.log.info("  (7)Verify pool read by: daos pool query --pool")
        self.verify_pool_readwrite(svc, uuid, "read", expect=read)

        # (8)Verify pool write operation
        #    daos continer create --pool <uuid>
        self.log.info("  (8)Verify pool write by: daos continer create --pool")
        self.verify_pool_readwrite(svc, uuid, "write", expect=write)
        if secondary_grp_test:
            self.log.info("  (8-0)Verifying verify_pool_acl_prim_sec_groups")
            self.verify_pool_acl_prim_sec_groups(pool_acl_list, acl_file,\
                uuid, svc)

        # (9)Cleanup user and destroy pool
        self.log.info("  (9)Cleanup users and groups")
        self.cleanup_user_group(num_user, num_group)
