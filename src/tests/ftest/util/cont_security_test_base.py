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
  provided in Contract No. 8F-30005.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""

import os
import pwd
import grp
import re
from apricot import TestWithServers
from avocado import fail_on

from daos_utils import DaosCommand
from command_utils import CommandFailure
import general_utils
from general_utils import DaosTestError
from test_utils_container import TestContainer

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


    @fail_on(CommandFailure)
    def create_pool_with_dmg(self):
        """Create a pool with the dmg tool

        Also, obtains the pool uuid and svc from the operation's
        result

        Returns:
            pool_uuid (str): Pool UUID, randomly generated.
            pool_svc (str): Pool service replica
        """
        self.prepare_pool()
        pool_uuid = self.pool.pool.get_uuid_str()
        pool_svc = self.pool.svc_ranks[0]

        return pool_uuid, pool_svc


    def create_container_with_daos(self, pool, acl_type=None):
        """Create a container with the daos tool

        Also, obtains the container uuid from the operation's result.

        Args:
            pool (TestPool): Pool object.
            acl_type (str, optional): valid or invalid.

        Returns:
            container_uuid: Container UUID created.
        """
        file_name = None
        get_acl_file = None
        expected_acl_types = [None, "valid", "invalid"]

        if acl_type not in expected_acl_types:
            self.fail(
                "    Invalid '{}' acl type passed.".format(acl_type))

        if acl_type:
            get_acl_file = "acl_{}.txt".format(acl_type)
            file_name = os.path.join(self.tmp, get_acl_file)
        else:
            get_acl_file = ""

        try:
            self.container = TestContainer(pool=pool,
                                           daos_command=self.daos_tool)
            self.container.get_params(self)
            self.container.create(acl_file=file_name)
            container_uuid = self.container.uuid
        except CommandFailure:
            if acl_type is not "invalid":
                raise DaosTestError(
                    "Could not create a container when expecting one")
            container_uuid = None

        return container_uuid


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
        if not general_utils.check_uuid_format(pool_uuid):
            self.fail(
                "    Invalid Pool UUID '{}' provided.".format(pool_uuid))

        if not general_utils.check_uuid_format(container_uuid):
            self.fail(
                "    Invalid Container UUID '{}' provided.".format(
                    container_uuid))

        result = self.daos_tool.container_get_acl(pool_uuid, pool_svc,
                                                  container_uuid, verbose,
                                                  outfile)

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
        for typ in types:
            get_acl_file = "acl_{}.txt".format(typ)
            file_name = os.path.join(self.tmp, get_acl_file)
            cmd = "rm -r {}".format(file_name)
            general_utils.run_command(cmd)
