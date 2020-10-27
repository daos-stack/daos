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
from security_test_base import read_acl_file
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
        self.daos_cmd = self.get_daos_command()
        self.prepare_pool()
        self.add_container(self.pool)

    @fail_on(CommandFailure)
    def test_get_acl_valid(self):
        """
        JIRA ID: DAOS-3705

        Test Description: Test that container get-acl command performs as
            expected with valid inputs and verify that we can't overwrite
            an already existing file when using the --outfile argument.

        :avocado: tags=all,pr,security,container_acl,cont_get_acl_inputs
        """
        test_errs = []
        for verbose in [True, False]:
            for outfile in self.params.get("valid_out_filename", "/run/*"):
                path_to_file = os.path.join(
                    self.tmp, "{}_{}".format(outfile, verbose))

                # Enable raising an exception if the daos command fails
                self.daos_cmd.exit_status_exception = False
                self.daos_cmd.container_get_acl(
                    self.pool.uuid,
                    self.pool.svc_ranks[0],
                    self.container.uuid,
                    verbose=verbose,
                    outfile=path_to_file)

                # Verify consistency of acl obtained through the file
                file_acl = read_acl_file(path_to_file)
                self.acl_file_diff(file_acl)

                # Let's verify that we can't overwrite an already existing file
                # Disable raising an exception if the daos command fails
                self.daos_cmd.exit_status_exception = False
                self.daos_cmd.container_get_acl(
                    self.pool.uuid,
                    self.pool.svc_ranks[0],
                    self.container.uuid,
                    verbose=verbose,
                    outfile=path_to_file)
                test_errs.extend(
                    self.error_handling(
                        self.daos_cmd.result, "File already exists"))

        if test_errs:
            self.fail("container get-acl command expected to fail: \
                {}".format("\n".join(test_errs)))

    def test_no_user_permissions(self):
        """
        JIRA ID: DAOS-3705

        Test Description: Test that container get-acl command doesn't
            get ACL information without permission.

        :avocado: tags=all,pr,security,container_acl,cont_get_acl_noperms
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
        self.daos_cmd.container_get_acl(
            self.pool.uuid,
            self.pool.svc_ranks[0],
            self.container.uuid,
            outfile="outfile.txt")
        test_errs.extend(self.error_handling(self.daos_cmd.result, "-1001"))

        if test_errs:
            self.fail("container get-acl command expected to fail: \
                {}".format("\n".join(test_errs)))
