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

    def create_container_with_daos(self, pool, acl_type=None, acl_file=None):
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

        if acl_file is None:
            if acl_type not in expected_acl_types:
                self.fail(
                    "    Invalid '{}' acl type passed.".format(acl_type))
            if acl_type:
                get_acl_file = "acl_{}.txt".format(acl_type)
                file_name = os.path.join(self.tmp, get_acl_file)
            else:
                get_acl_file = ""
        else:
            file_name = acl_file

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

    def overwrite_container_acl(self, acl_file):
        """Overwrite existing container acl-entries with acl_file.

        Args:
            acl_file (str): acl filename.

        Return:
            result (str): daos_tool.container_overwrite_acl.
        """
        self.daos_tool.exit_status_exception = False
        result = self.daos_tool.container_overwrite_acl(
            self.pool_uuid, self.pool_svc, self.container_uuid, acl_file)
        return result

    def update_container_acl(self, entry):
        """Update container acl entry.

        Args:
            entry (str): acl entry to be updated.

        Return:
            result (str): daos_tool.container_update_acl.
        """
        self.daos_tool.exit_status_exception = False
        result = self.daos_tool.container_update_acl(
            self.pool_uuid, self.pool_svc, self.container_uuid, entry=entry)
        return result

    def test_container_destroy(self, pool_uuid, pool_svc, container_uuid):
        """Test container destroy/delete.

        Args:
            pool_uuid (str): pool uuid.
            pool_svc  (str): pool service replica.
            container_uuid (str): container uuid.

        Return:
            result (str): daos_tool.container_destroy result.
        """
        self.daos_tool.exit_status_exception = False
        result = self.daos_tool.container_destroy(
            pool_uuid, pool_svc, container_uuid, True)
        return result

    def set_container_attribute(
            self, pool_uuid, pool_svc, container_uuid, attr, value):
        """Write/Set container attribute.

        Args:
            pool_uuid (str): pool uuid.
            pool_svc  (str): pool service replica.
            container_uuid (str): container uuid.
            attr (str): container attribute.
            value (str): container attribute value to be set.

        Return:
            result (str): daos_tool.container_set_attr result.
        """
        self.daos_tool.exit_status_exception = False
        result = self.daos_tool.container_set_attr(
            pool_uuid, container_uuid, attr, value, pool_svc)
        return result

    def get_container_attribute(
            self, pool_uuid, pool_svc, container_uuid, attr):
        """Get container attribute.

        Args:
            pool_uuid (str): pool uuid.
            pool_svc  (str): pool service replica.
            container_uuid (str): container uuid.
            attr (str): container attribute.

        Return:
            CmdResult: Object that contains exit status, stdout, and other
                information.
        """
        self.daos_tool.exit_status_exception = False
        self.daos_tool.container_get_attr(
            pool_uuid, container_uuid, attr, pool_svc)
        return self.daos_tool.result

    def list_container_attribute(
            self, pool_uuid, pool_svc, container_uuid):
        """List container attribute.

        Args:
            pool_uuid (str): pool uuid.
            pool_svc  (str): pool service replica.
            container_uuid (str): container uuid.

        Return:
            result (str): daos_tool.container_list_attrs result.
        """
        self.daos_tool.exit_status_exception = False
        result = self.daos_tool.container_list_attrs(
            pool_uuid, container_uuid, pool_svc)
        return result


    def set_container_property(
            self, pool_uuid, pool_svc, container_uuid, prop, value):
        """Write/Set container property.

        Args:
            pool_uuid (str): pool uuid.
            pool_svc  (str): pool service replica.
            container_uuid (str): container uuid.
            prop (str): container property name.
            value (str): container property value to be set.

        Return:
            result (str): daos_tool.container_set_prop result.
        """
        self.daos_tool.exit_status_exception = False
        result = self.daos_tool.container_set_prop(
            pool_uuid, container_uuid, prop, value, pool_svc)
        return result

    def get_container_property(self, pool_uuid, pool_svc, container_uuid):
        """Get container property.

        Args:
            pool_uuid (str): pool uuid.
            pool_svc  (str): pool service replica.
            container_uuid (str): container uuid.

        Return:
            result (str): daos_tool.container_get_prop.
        """
        self.daos_tool.exit_status_exception = False
        result = self.daos_tool.container_get_prop(
            pool_uuid, container_uuid, pool_svc)
        return result

    def set_container_owner(
            self, pool_uuid, pool_svc, container_uuid, user, group):
        """Set container owner.

        Args:
            pool_uuid (str): pool uuid.
            pool_svc  (str): pool service replica.
            container_uuid (str): container uuid.
            user (str): container user-name to be set owner to.
            group (str): container group-name to be set owner to.

        Return:
            result (str): daos_tool.container_set_owner.
        """
        self.daos_tool.exit_status_exception = False
        result = self.daos_tool.container_set_owner(
            pool_uuid, container_uuid, user, group, pool_svc)
        return result

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

        exp_list = expected_list[:]
        if len(get_acl_list) != len(exp_list):
            return False
        for acl in get_acl_list:
            if acl in exp_list:
                exp_list.remove(acl)
            else:
                return False
        return True

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

    def error_handling(self, results, err_msg):
        """Handle errors when test fails and when command unexpectedly passes.

        Args:
            results (CmdResult): object containing stdout, stderr and
                exit status.
            err_msg (str): error message string to look for in stderr.

        Returns:
            list: list of test errors encountered.

        """
        test_errs = []
        if results.exit_status == 0:
            test_errs.append("{} passed unexpectedly: {}".format(
                results.command, results.stdout))
        elif results.exit_status == 1:
            # REMOVE BELOW IF Once DAOS-5635 is resolved
            if results.stdout and err_msg in results.stdout:
                self.log.info("Found expected error %s", results.stdout)
            # REMOVE ABOVE IF Once DAOS-5635 is resolved
            elif results.stderr and err_msg in results.stderr:
                self.log.info("Found expected error %s", results.stderr)
            else:
                self.fail("{} seems to have failed with \
                    unexpected error: {}".format(results.command, results))
        return test_errs

    def acl_file_diff(self, prev_acl, flag=True):
        """Helper function to compare current content of acl-file.

        If provided  prev_acl file information is different from current acl
        file information test will fail if flag=True. If flag=False, test will
        fail in the case that the acl contents are found to have no difference.

        Args:
            prev_acl (list): list of acl entries within acl-file.
                Defaults to True.
            flag (bool, optional): if True, test will fail when acl-file
                contents are different, else test will fail when acl-file
                contents are same. Defaults to True.
        """
        current_acl = self.get_container_acl_list(
            self.pool.uuid, self.pool.svc_ranks[0], self.container.uuid)
        if self.compare_acl_lists(prev_acl, current_acl) != flag:
            self.fail("Previous ACL:\n{} \nPost command ACL:\n{}".format(
                prev_acl, current_acl))
