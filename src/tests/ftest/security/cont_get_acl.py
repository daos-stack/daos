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
from command_utils import CommandFailure
from avocado import fail_on


class GetContainerACLTest(ContSecurityTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:

    Test to verify container ACL get command..

    :avocado: recursive
    """
    def setUp(self):
        """Set up each test case."""
        super(GetContainerACLTest, self).setUp()
        self.acl_filename = "test_acl_file.txt"
        self.daos_cmd = self.get_daos_command()
        self.prepare_pool()
        self.add_container(self.pool)

        # Get list of ACL entries
        self.cont_acl = self.get_container_acl_list(
            self.pool.uuid, self.pool.svc_ranks[0], self.container.uuid)

    def error_handling(self, results, err_msg):
        """Handle errors when test fails and when command unexpectedly passes.

        Args:
            results (CmdResult): object containing stdout, stderr and
                exit status
            err_msg (str): error message string to look for in stderr.

        Returns:
            list: list of test errors encountered.
        """
        test_errs = []
        if results.exit_status == 0:
            test_errs.append("get-acl passed unexpectedly: {}".format(
                results.stdout))
        elif results.exit_status == 1:
            # REMOVE BELOW IF Once DAOS-5635 is resolved
            if results.stdout and err_msg in results.stdout:
                self.log.info("Found expected error %s", results.stdout)
            # REMOVE ABOVE IF Once DAOS-5635 is resolved
            elif results.stderr and err_msg in results.stderr:
                self.log.info("Found expected error %s", results.stderr)
            else:
                self.fail("get-acl seems to have failed with \
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

    def read_acl_file(self, filename):
        """Read contents of given acl file.

        Args:
            filename: name of file to be read for acl information

        Returns:
            list: list containing ACL entries

        """
        f = open(filename, 'r')
        content = f.readlines()
        f.close()

        # Parse
        acl = []
        for entry in content:
            if not entry.startswith("#"):
                acl.append(entry.strip())

        return acl

    @fail_on(CommandFailure)
    def test_acl_get_valid(self):
        """
        JIRA ID: DAOS-3705

        Test Description: Test that container get command performs as
            expected with invalid inputs.

        :avocado: tags=all,pr,security,container_acl,cont_get_acl_inputs
        """
        # Get list of outfile filenames to put contents of ACL in
        out_filenames = self.params.get("out_filename", "/run/*")
        path_to_file = os.path.join(self.tmp, self.acl_filename)

        # Disable raising an exception if the daos command fails
        self.daos_cmd.exit_status_exception = False

        for verbose in [True, False]:
            for outfile in out_filenames:
                self.daos_cmd.container_get_acl(
                    self.pool.uuid,
                    self.pool.svc_ranks[0],
                    self.container.uuid,
                    verbose=verbose,
                    outfile=outfile)

                file_acl = self.read_acl_file(path_to_file)

                # Verify consistency of acl obtained through the file
                self.acl_file_diff(file_acl)

    def test_no_user_permissions(self):
        """
        JIRA ID: DAOS-3705

        Test Description: Test that container get-acl command doesn't
            get ACL information without permission.

        :avocado: tags=all,pr,security,container_acl,cont_get_acl
        """
        # Let's give access to the pool to the root user
        self.get_dmg_command().pool_update_acl(
            self.pool.uuid, entry="A::EVERYONE@:rw")

        # The root user shouldn't have access to getting container ACL entries
        self.daos_cmd.sudo = True

        # Disable raising an exception if the daos command fails
        self.daos_cmd.exit_status_exception = False

        # Let's check that we can't run as root (or other user) and get
        # acl information if no permissions are set for that user.
        test_errs = []
        for verbose in [True, False]:
            for outfile in self.params.get("out_filename", "/run/*"):
                self.daos_cmd.container_get_acl(
                    self.pool.uuid,
                    self.pool.svc_ranks[0],
                    self.container.uuid,
                    verbose=verbose,
                    outfile=outfile)
                test_errs.extend(
                    self.error_handling(self.daos_cmd.result, "-1001"))
        if test_errs:
            self.fail("container get-acl command expected to fail: \
                {}".format("\n".join(test_errs)))
