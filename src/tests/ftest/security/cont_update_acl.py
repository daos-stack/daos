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

from cont_security_test_base import ContSecurityTestBase
from security_test_base import create_acl_file
from command_utils import CommandFailure
from avocado import fail_on


class UpdateContainerACLTest(ContSecurityTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:
    Test to verify ACL entry update.
    :avocado: recursive
    """
    def setUp(self):
        """Set up each test case."""
        super(UpdateContainerACLTest, self).setUp()
        self.acl_filename = "test_acl_file.txt"
        self.daos_cmd = self.get_daos_command()
        self.prepare_pool()
        self.add_container(self.pool)

        # List of ACL entries
        self.cont_acl = self.get_container_acl_list(
            self.pool.uuid, self.container.uuid)

    def test_acl_update_invalid_inputs(self):
        """
        JIRA ID: DAOS-3711

        Test Description: Test that container update command performs as
            expected with valid and invalid inputs in command line for the
            --entry and --acl-file options.

        :avocado: tags=all,pr,security,container_acl,cont_update_acl_inputs
        """
        # Get lists of invalid
        invalid_entries = self.params.get("invalid_acl_entries", "/run/*")
        invalid_filename = self.params.get("invalid_acl_filename", "/run/*")

        # Disable raising an exception if the daos command fails
        self.daos_cmd.exit_status_exception = False

        test_errs = []
        for entry in invalid_entries:

            # Run update command
            self.daos_cmd.container_update_acl(
                self.pool.uuid,
                self.container.uuid,
                entry=entry)
            test_errs.extend(self.error_handling(self.daos_cmd.result, "-1003"))

            # Check that the acl file was unchanged
            self.acl_file_diff(self.cont_acl)

        for acl_file in invalid_filename:

            # Run update command
            self.daos_cmd.container_update_acl(
                self.pool.uuid,
                self.container.uuid,
                acl_file=acl_file)
            test_errs.extend(self.error_handling(
                self.daos_cmd.result, "No such file or directory"))

            # Check that the acl file was unchanged
            self.acl_file_diff(self.cont_acl)

        if test_errs:
            self.fail("container update-acl command expected to fail: \
                {}".format("\n".join(test_errs)))

    def test_update_invalid_acl(self):
        """JIRA ID: DAOS-3711

        Test Description: Test that container update command performs as
            expected with invalid values within ACL file.

        :avocado: tags=all,pr,security,container_acl,cont_update_invalid_acl
        """
        invalid_file_content = self.params.get(
            "invalid_acl_file_content", "/run/*")
        path_to_file = os.path.join(self.tmp, self.acl_filename)

        # Disable raising an exception if the daos command fails
        self.daos_cmd.exit_status_exception = False

        test_errs = []
        for content in invalid_file_content:
            create_acl_file(path_to_file, content)

            # Run update command
            self.daos_cmd.container_update_acl(
                self.pool.uuid,
                self.container.uuid,
                path_to_file)
            test_errs.extend(self.error_handling(self.daos_cmd.result, "-1003"))

            # Check that the acl file was unchanged
            self.acl_file_diff(self.cont_acl)

        if test_errs:
            self.fail("container update-acl command expected to fail: \
                {}".format("\n".join(test_errs)))

    @fail_on(CommandFailure)
    def test_update_acl_file(self):
        """
        JIRA ID: DAOS-3711

        Test Description: Test that container update command performs as
            expected when adding an ACL file that contains principals that are
            currently in the ACL.

        :avocado: tags=all,pr,security,container_acl,cont_update_acl
        """
        path_to_file = os.path.join(self.tmp, self.acl_filename)

        # Create acl file to use for update
        ace_to_add = ["A:G:my_great_test@:rw", "A::my_new_principal@:rwTao"]
        create_acl_file(path_to_file, self.cont_acl + ace_to_add)

        # Run update command
        self.daos_cmd.container_update_acl(
            self.pool.uuid,
            self.container.uuid,
            acl_file=path_to_file)

        # Verify that the entry added did not affect any other entry
        self.acl_file_diff(self.cont_acl + ace_to_add)

        # Let's add a file with existing principals and verify overridden values
        ace_to_add_2 = ["A:G:my_great_test@:rwd", "A::my_new_principal@:rw"]
        create_acl_file(path_to_file, ace_to_add_2)

        # Run update command
        self.daos_cmd.container_update_acl(
            self.pool.uuid,
            self.container.uuid,
            acl_file=path_to_file)

        # Verify that the ACL file is now composed of the updated ACEs
        self.acl_file_diff(self.cont_acl + ace_to_add_2)

        # Lastly, let's add a file that contains only new principals
        ace_to_add_3 = ["A:G:new_new_principal@:rwd", "A::last_one@:rw"]
        create_acl_file(path_to_file, ace_to_add_3)

        # Run update command
        self.daos_cmd.container_update_acl(
            self.pool.uuid,
            self.container.uuid,
            acl_file=path_to_file)

        # Verify that the ACL file is now composed of the updated ACEs
        self.acl_file_diff(self.cont_acl + ace_to_add_2 + ace_to_add_3)

    def test_no_user_permissions(self):
        """
        JIRA ID: DAOS-3711

        Test Description: Test that container update command fails with
            no permission -1001 when user doesn't have the right permissions.

        :avocado: tags=all,pr,security,container_acl,cont_update_acl_noperms
        """
        valid_file_content = self.params.get("valid_acl_file", "/run/*")
        path_to_file = os.path.join(self.tmp, self.acl_filename)

        # Let's give access to the pool to the root user
        self.get_dmg_command().pool_update_acl(
            self.pool.uuid, entry="A::EVERYONE@:rw")

        # The root user shouldn't have access to updating container ACL entries
        self.daos_cmd.sudo = True

        # Disable raising an exception if the daos command fails
        self.daos_cmd.exit_status_exception = False

        # Let's check that we can't run as root (or other user) and update
        # entries if no permissions are set for that user.
        test_errs = []
        for content in valid_file_content:
            create_acl_file(path_to_file, content)
            self.daos_cmd.container_update_acl(
                self.pool.uuid,
                self.container.uuid,
                path_to_file)
            test_errs.extend(self.error_handling(self.daos_cmd.result, "-1001"))

            # Check that the acl file was unchanged
            self.acl_file_diff(self.cont_acl)

        if test_errs:
            self.fail("container update-acl command expected to fail: \
                {}".format("\n".join(test_errs)))
