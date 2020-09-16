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
from avocado.utils import process
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
        self.daos_cmd = self.get_daos_command()
        self.prepare_pool()
        self.add_container(self.pool)

        # List of ACL entries
        self.cont_acl = self.get_container_acl_list(
            self.pool.uuid, self.pool.svc_ranks[0], self.container.uuid)

    def error_handling(self, results, err_msg):
        """Handle errors when test fails and when command unexpectedly passes.

        Args:
            results (CmdResult): object containing stdout, stderr and
                exit status

        Returns:
            list: list of test errors encountered.

        """
        test_errs = []
        if results.exit_status == 0:
            test_errs.append("overwrite-acl passed unexpectedly: {}".format(
                results.stdout))
        elif results.exit_status == 1:
            # REMOVE BELOW IF Once DAOS-5635 is resolved
            if results.stdout and err_msg in results.stdout:
                self.log.info("Found expected error %s", results.stdout)
            # REMOVE ABOVE IF Once DAOS-5635 is resolved
            elif results.stderr and err_msg in results.stderr:
                self.log.info("Found expected error %s", results.stderr)
            else:
                self.fail("overwrite-acl seems to have failed with \
                    unexpected error: {}".format(results))
        return test_errs

    def acl_file_diff(self, prev_acl, flag=True):
        """Helper function to compare current content of acl-file.

        If provided  prev_acl file information is different from current acl
        file information test will fail if flag=True. If flag=False, test will
        fail in the case that the acl contents are found to have no difference.

        Args:
            prev_acl (list): list of acl entries within acl-file.
                Defaults to True.
            flag (bool): if True, test will fail when acl-file contents are
                different, else test will fail when acl-file contents are same.
        """
        current_acl = self.get_container_acl_list(
            self.pool.uuid, self.pool.svc_ranks[0], self.container.uuid)
        if self.compare_acl_lists(prev_acl, current_acl) != flag:
            self.fail("Previous ACL:\n{} \nPost command ACL:\n{}".format(
                prev_acl, current_acl))

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
                self.pool.svc_ranks[0],
                self.container.uuid,
                entry=entry)
            test_errs.extend(self.error_handling(self.daos_cmd.result, "-1003"))

            # Check that the acl file was unchanged
            self.acl_file_diff(self.cont_acl)

        for acl_file in invalid_filename:

            # Run update command
            self.daos_cmd.container_update_acl(
                self.pool.uuid,
                self.pool.svc_ranks[0],
                self.container.uuid,
                acl_file=acl_file)
            test_errs.extend(self.error_handling(
                self.daos_cmd.result, "No such file or directory"))

            # Check that the acl file was unchanged
            self.acl_file_diff(self.cont_acl)

        if test_errs:
            self.fail("container update-acl command expected to fail: \
                {}".format("\n".join(test_errs)))

    def test_update_invalid_acl_file(self):
        """
        JIRA ID: DAOS-3711

        Test Description: Test that container update command performs as
            expected with invalid inputs in command line and within ACL file
            provided.

        :avocado: tags=all,pr,security,container_acl,cont_update_acl
        """
        acl_filename = "test_acl_file.txt"
        invalid_file_content = self.params.get(
            "invalid_acl_file_content", "/run/*")

        for content in invalid_file_content:
            path_to_file = os.path.join(os.getcwd(), acl_filename)
            create_acl_file(path_to_file, content)

            # Run update command
            try:
                self.daos_cmd.container_update_acl(
                    self.pool.uuid,
                    self.pool.svc_ranks[0],
                    self.container.uuid,
                    path_to_file)
            except process.CmdError as err:
                if "-1003" in err.result.stdout:
                    self.log.info(
                        "Found expected error %s with invalid file content %s",
                        err.result.stdout, content)
                else:
                    self.fail("update-acl seems to have failed with \
                        unexpected error: {}".format(err))

    @fail_on(CommandFailure)
    def test_update_valid_acl_file(self):
        """
        JIRA ID: DAOS-3711
        Test Description: Test that container update command performs as
            expected with valid ACL file provided.
        :avocado: tags=all,pr,security,container_acl,cont_update_acl
        """
        acl_filename = "test_acl_file.txt"
        valid_file_content = self.params.get(
            "valid_acl_file", "/run/*")
        path_to_file = os.path.join(os.getcwd(), acl_filename)

        for content in valid_file_content:
            create_acl_file(path_to_file, content)

            # Run update command, test will fail if command fails.
            self.daos_cmd.container_update_acl(
                self.pool.uuid,
                self.pool.svc_ranks[0],
                self.container.uuid,
                path_to_file)

    def test_no_user_permissions(self):
        """
        JIRA ID: DAOS-3711
        Test Description: Test that container update command fails with
            no permission -1001 when user doesn't have the right permissions.
        :avocado: tags=all,pr,security,container_acl,cont_update_acl
        """
        acl_filename = "test_acl_file.txt"
        valid_file_content = self.params.get(
            "valid_acl_file", "/run/*")
        path_to_file = os.path.join(os.getcwd(), acl_filename)

        # Let's give access to the pool to the root user
        self.get_dmg_command().pool_update_acl(
            self.pool.uuid, entry="A::EVERYONE@:rw")

        # The root user shouldn't have access to deleting container ACL entries
        self.daos_cmd.sudo = True

        # Let's check that we can't run as root (or other user) and update
        # entries if no permissions are set for that user.
        for content in valid_file_content:
            create_acl_file(path_to_file, content)
            try:
                self.daos_cmd.container_update_acl(
                    self.pool.uuid,
                    self.pool.svc_ranks[0],
                    self.container.uuid,
                    path_to_file)
            except process.CmdError as err:
                if "-1001" in err.result.stdout:
                    self.log.info(
                        "Found expected error %s with %s",
                        err.result.stdout, path_to_file)
                else:
                    self.fail("update-acl seems to have failed with \
                        unexpected error: {}".format(err))
        self.fail("container update-acl command expected to fail.")
