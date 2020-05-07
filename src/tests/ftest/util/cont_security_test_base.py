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
import re
from apricot import TestWithServers
from avocado import fail_on

import dmg_utils
from daos_utils import DaosCommand
from command_utils import CommandFailure
import security_test_base as secTestBase
#TODO: Uncomment 'general_utils' once the PR1484 has landed.
#import general_utils

PERMISSIONS = ["r", "w", "rw", "rwc", "rwcd", "rwcdt", "rwcdtT"]

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
        self.current_user = pwd.getpwuid(self.user_uid)[0]
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
            List of permissions of container
        """
        # First we determine the type o acl to be created
        if acl_type == "default":
            acl_entries = ["A::OWNER@:rwdtTaAo",
                           "A:G:GROUP@:rwtT"]
        elif acl_type == "valid":
            acl_entries = ["A::OWNER@:rwdtTaAo",
                           secTestBase.acl_entry("user", self.current_user, "random", PERMISSIONS),
                           "A:G:GROUP@:rwtT",
                           secTestBase.acl_entry("group", self.current_group, "random", PERMISSIONS),
                           "A::EVERYONE@:"]
        elif acl_type == "invalid":
            acl_entries = ["A::OWNER@:invalid",
                           "A:G:GROUP@:rwtT"]
        else:
            acl_entries = None
            self.fail("    Invalid acl_type while generating permissions")

        # We now write the default acl file
        get_acl_file = "acl_" + acl_type + ".txt"
        file_name = os.path.join(self.tmp, get_acl_file)
        acl_file = open(file_name, "w")
        acl_file.write("\n".join(acl_entries))
        acl_file.close()
        return acl_entries


    @fail_on(CommandFailure)
    def create_pool_with_dmg(self):
        """Create a pool with the dmg tool and verify its status.

        Also, obtains the pool uuid and svc from the operation's
        result

        Returns:
            pool_uuid (str): Pool UUID, randomly generated.
            pool_svc (str): Pool service replica
        """
        scm_size = self.params.get("scm_size", "/run/pool*")
        result = self.dmg.pool_create(scm_size)
        self.log.info("    dmg = %s", self.dmg)

        # Verify the pool create status
        self.log.info("    dmg.run() result =\n%s", result)
        if "ERR" not in result.stderr:
            pool_uuid, pool_svc = \
                dmg_utils.get_pool_uuid_service_replicas_from_stdout(
                    result.stdout)
        else:
            self.fail("    Unable to parse the Pool's UUID and SVC.")
            pool_uuid = None
            pool_svc = None

        return pool_uuid, pool_svc


    @fail_on(CommandFailure)
    def destroy_pool_with_dmg(self):
        """Destroy a pool with the dmg tool and verify its status.

        """
        result = self.dmg.pool_destroy(pool=self.pool_uuid)
        self.log.info("    dmg = %s", self.dmg)

        # Verify the pool destroy status
        self.log.info("    dmg.run() result =\n%s", result)
        if "failed" in result.stdout:
            self.fail("    Unable to destroy pool")
        else:
            self.pool_uuid = None
            self.pool_svc = None


    def create_container_with_daos(self, pool_uuid, pool_svc, acl_type=None):
        """Create a container with the daos tool and verify its status.

        Also, obtains the container uuid from the operation's result.

        Args:
            pool_uuid (str): Pool uuid.
            pool_svc (str): Pool service replicas.
            acl_type (str, optional): valid or invalid.

        Returns:
            container_uuid: Container UUID created or None.
        """
        if not secTestBase.check_uuid_format(pool_uuid):
            self.fail(
                "    Invalid Pool UUID '%s' provided.", pool_uuid)

        file_name = None
        get_acl_file = None
        expected_acl_types = [None, "valid", "invalid"]

        if acl_type not in expected_acl_types:
            self.fail(
                "    Invalid '%s' acl type passed.", acl_type)

        if acl_type:
            get_acl_file = "acl_{}.txt".format(acl_type)
            file_name = os.path.join(self.tmp, get_acl_file)
        else:
            get_acl_file = ""

        if acl_type == "invalid":
            self.daos_tool.exit_status_exception = False

        kwargs = {"pool": pool_uuid,
                  "svc": pool_svc,
                  "acl_file": file_name}
        container = \
            self.daos_tool.get_output("container_create", **kwargs)
        if len(container) == 1:
            container_uuid = container[0]
        else:
            container_uuid = None

        if acl_type == "invalid":
            self.daos_tool.exit_status_exception = True

        self.log.info("    daos = %s", self.daos_tool)

        return container_uuid


    @fail_on(CommandFailure)
    def destroy_container_with_daos(self, container_uuid):
        """Destroy a container with the daos tool and verify its status.

        Args:
            container_uuid (str): The UUID of the container to be destroyed.

        Returns:
            True or False if Container was destroyed or not.

        """
        if not secTestBase.check_uuid_format(container_uuid):
            self.fail(
                "    Invalid Container UUID '%s' provided.", container_uuid)

        result = self.daos_tool.container_destroy(pool=self.pool_uuid,
                                                  svc=self.pool_svc,
                                                  cont=self.container_uuid)

        self.log.info("    daos = %s", self.daos_tool)

        # Verify the container destroy status
        self.log.info("    daos.run() result =\n%s", result)
        if "failed" in result.stdout:
            self.log.info("    Unable to destroy container")
            return False
        else:
            return True


    def get_container_acl_list(self, pool_uuid, pool_svc, container_uuid,
                               verbose=False, outfile=None):
        """Get daos container acl list by daos container get-acl.

        Args:
            pool_uuid (str): Pool uuid.
            pool_svc (str): Pool service replicas.
            container_uuid (str): Container uuid.
            verbose (bool, optional): Verbose mode.
            outfile (str, optional): Write ACL to file

        Return:
            cont_permission_list: daos container acl list.

        """
        if not secTestBase.check_uuid_format(pool_uuid):
            self.fail(
                "    Invalid Pool UUID '%s' provided.", pool_uuid)

        if not secTestBase.check_uuid_format(container_uuid):
            self.fail(
                "    Invalid Container UUID '%s' provided.", container_uuid)

        result = self.daos_tool.container_get_acl(pool_uuid, pool_svc,
                                    container_uuid, verbose, outfile)

        cont_permission_list = []
        for line in result.stdout.splitlines():
            if not line.startswith("A:"):
                continue
            elif line.startswith("A::"):
                found_user = re.search(r"A::(.+)@:(.*)", line)
                if found_user:
                    cont_permission_list.append(line)
            elif line.startswith("A:G:"):
                found_group = re.search(r"A:G:(.+)@:(.*)", line)
                if found_group:
                    cont_permission_list.append(line)
        return cont_permission_list


    def compare_acl_lists(self, get_acl_list, expected_list):
        """Compares two permission lists

        Args:
            get_acl_list (str list): list of permissions obtained by get-acl
            expected_list (str list): list of expected permissions

        Returns:
            True or False if both permission lists are identical or not
        """
        self.log.info("    ===> get-acl ACL:  %s", get_acl_list)
        self.log.info("    ===> Expected ACL: %s", expected_list)

        if len(get_acl_list) == len(expected_list):
            for element in get_acl_list:
                if element in expected_list:
                    continue
                else:
                    return False
            return True
        return False


    def cleanup(self, types):
        """Removes all temporal acl files created during the test.

        Args:
            types (list): types of acl files [valid, invalid]

        """
        for t in types:
            get_acl_file = "acl_{}.txt".format(t)
            file_name = os.path.join(self.tmp, get_acl_file)
            cmd = "rm -r {}".format(file_name)
            #TODO: Replace the following line with the commented one
            #      once the PR1484 has landed.
            secTestBase.run_command(cmd)
            #general_utils.run_command(cmd)
