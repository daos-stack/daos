#!/usr/bin/python
"""
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
"""

import os
import pwd
import grp
import random
from apricot import TestWithServers

import daos_utils
from daos_utils import DaosCommand
import dmg_utils
from dmg_utils import DmgCommand

PERMISSIONS = ["", "r", "w", "rw", "rwc", "rwcd", "rwcdt", "rwcdtT"]


def acl_entry(usergroup, name, permission):
    """Create a daos acl entry for the specified user or group and permission.

    Args:
        usergroup (str): user or group.
        name (str): user or group name to be created.
        permission (str): permission to be created.

    Return:
        str: daos pool acl entry.

    """
    if permission == "random":
        permission = random.choice(PERMISSIONS)
    if permission == "nonexist":
        return ""
    if "group" in usergroup:
        entry = "A:G:" + name + "@:" + permission
    else:
        entry = "A::" + name + "@:" + permission
    return entry


class ContSecurityTestBase(TestWithServers):
    """Container security test cases.

    Test Class Description:
        Test methods to verify the Container security with acl by
        using daos tool.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ContSecurityTestBase object."""
        super(ContSecurityTestBase, self).__init__(*args, **kwargs)
        self.dmg = None
        self.daos_tool = None
        self.user_uid = None
        self.user_gid = None
        self.current_user = None
        self.current_group = None
        self.pool_uuid = None
        self.pool_svc = None
        self.container_uuid = None


    def setUp(self):
        """Set up each test case."""
        super(ContSecurityTestBase, self).setUp()
        self.user_uid = os.geteuid()
        self.user_gid = os.getegid()
        self.current_user  = pwd.getpwuid(self.user_uid)[0]
        self.current_group = grp.getgrgid(self.user_uid)[0]
        self.co_prop = self.params.get("container_properties",
                                       "/run/container/*")
        self.dmg = self.get_dmg_command()
        self.daos_tool = DaosCommand(self.bin)


    def generate_acl_file(self, acl_type):
        """Create an acl file for the specified type.
        
        Args:
            acl_type (str): default, invalid, valid

        Returns:
            none.
        """
        # First we determine the type o acl to be created
        if acl_type == "default":
            acl_entries = ["A::OWNER@:rwdtTaAo",
                           "A:G:GROUP@:rwtT"]
        elif acl_type == "valid":
            acl_entries = ["A::OWNER@:rwdtTaAo",
                           acl_entry("user", self.current_user, "random"),
                           "A:G:GROUP@:rwtT",
                           acl_entry("group", self.current_group, "random"),
                           "A::EVERYONE@:"]
        elif acl_type == "invalid":
            acl_entries = ["A::OWNER@:invalid",
                           "A:G:GROUP@:rwtT"]
        else:
            self.fail("    Invalid acl_type while generating permissions")

        # We now write the default acl file
        get_acl_file = "acl_" + acl_type + ".txt"
        file_name = os.path.join(self.tmp, get_acl_file)
        acl_file = open(file_name, "w")
        acl_file.write("\n".join(acl_entries))
        acl_file.close()


    def create_pool_with_dmg(self):
        """Create a pool with the dmg tool and verify its status.

        Also, obtains the pool uuid and svc from the operation's
        result

        Args:
            none.

        Returns:
            none.
        """
        scm_size = self.params.get("scm_size", "/run/pool*")
        self.dmg.exit_status_exception = False
        result = self.dmg.pool_create(scm_size)
        self.dmg.exit_status_exception = True
        self.log.info("    dmg = %s", self.dmg)

        # Verify the pool create status
        self.log.info("    dmg.run() result =\n%s", result)
        if "ERR" not in result.stderr:
            self.pool_uuid, self.pool_svc = \
                dmg_utils.get_pool_uuid_service_replicas_from_stdout(
                    result.stdout)
        else:
            self.fail("    Unable to parse the Pool's UUID and SVC.")
            self.pool_uuid = None
            self.pool_svc = None


    def destroy_pool_with_dmg(self):
        """Destroy a pool with the dmg tool and verify its status.

        Args:
            none.
        
        Returns:
            none.
        """
        self.dmg.exit_status_exception = False
        result = self.dmg.pool_destroy(pool=self.pool_uuid)
        self.dmg.exit_status_exception = True
        self.log.info("    dmg = %s", self.dmg)

        # Verify the pool destroy status
        self.log.info("    dmg.run() result =\n%s", result)
        if "failed" in result.stdout:
            self.fail("    Unable to destroy pool")
        else:
            self.pool_uuid = None
            self.pool_svc = None


    def create_container_with_daos(self, acl_type=None):
        """Create a container with the daos tool and verify its status.

        Also, obtains the container uuid from the operation's result.

        Args:
            acl_type (str): valid or invalid

        Returns:
            none.
        """
        file_name = None
        get_acl_file = None

        if acl_type == None:
            get_acl_file = ""
        else:
            get_acl_file = "acl_{}.txt".format(acl_type)
            file_name = os.path.join(self.tmp, get_acl_file)

        self.daos_tool.exit_status_exception = False
        kwargs = {"pool": self.pool_uuid,
                  "svc": self.pool_svc,
                  "acl_file": file_name}
        container = \
            self.daos_tool.get_output("container_create", **kwargs)
        if len(container) == 1:
            self.container_uuid = container[0]
        else:
            self.container_uuid = None
        self.daos_tool.exit_status_exception = True
        self.log.info("    daos = %s",self.daos_tool)

        ## Verify the daos container status
        if acl_type == "invalid":
            if self.container_uuid is not None:
                self.fail("    Container [%s] created when no expecting one.", self.container_uuid)
        else:
            if self.container_uuid is None:
                self.fail("    Expecting a container to be created but none was.")


    def destroy_container_with_daos(self):
        """Destroy a container with the daos tool and verify its status

        Args:
            none

        Returns:
            none
        """
        self.daos_tool.exit_status_exception = False
        result = self.daos_tool.container_destroy(pool=self.pool_uuid,
                                                  svc=self.pool_svc,
                                                  cont=self.container_uuid)
        self.daos_tool.exit_status_exception = True
        self.log.info("    daos = %s", self.daos_tool)

        # Verify the container destroy status
        self.log.info("    daos.run() result =\n%s", result)
        if "failed" in result.stdout:
            self.fail("    Unable to destroy container")
        else:
            self.container_uuid = None

            
    def verify_daos_cont_result(self, result, action, expect, err_code):
        """Verify the daos create result.
        
        Args:
            result (CmdResult): handle for daos container action.
            action (str): daos container 'create'.
            expect (str): pass or fail.
            err_code (str): expecting error code.

        Return:
            none.
        """
        if expect.lower() == 'pass':
            if result.exit_status != 0 or result.stderr != "":
                self.fail(
                    "    ##Test Fail on verify_daos_cont_result {}, expected Pass, but "
                    "Failed.".format(action))
            else:
                self.log.info(
                    "    Test Passed on verify_daos_cont_result %s, Succeed.\n", action)
        elif err_code not in result.stderr:
            self.fail(
                "    ##Test Fail on verify_daos_cont_result {}, expected Failure of {}, "
                "but Passed.".format(action, expect))
        else:
            self.log.info(
                "    Test Passed on verify_daos_cont_result %s expected error of '%s'.\n",
                action, expect)


    def cleanup(self, types):
        """Removes all temporal acl files created during the test.

        Args:
            types (list): types of acl files [valid, invalid]

        Returns:
            none.
        """
        for t in types:
            get_acl_file = "acl_{}.txt".format(t)
            file_name = os.path.join(self.tmp, get_acl_file)
            cmd = "rm -r {}".format(file_name)
            execute_command(cmd)


def execute_command(command):
    """Executes a command

    Args:
        command (str): Command to be executed.

    Returns:
        none.
    """
    os.system(command)

