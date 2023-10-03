"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from avocado import fail_on

from cont_security_test_base import ContSecurityTestBase
from security_test_base import read_acl_file
from exception_utils import CommandFailure


class GetContainerACLTest(ContSecurityTestBase):
    """Test Class Description:

    Test to verify container ACL get command..

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        super().setUp()
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

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,container,container_acl,daos_cmd
        :avocado: tags=GetContainerACLTest,test_get_acl_valid
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
                    self.container.uuid,
                    verbose=verbose,
                    outfile=path_to_file)
                test_errs.extend(
                    self.error_handling(
                        self.daos_cmd.result, "ile exists"))

        if test_errs:
            self.fail("container get-acl command expected to fail: \
                {}".format("\n".join(test_errs)))

    def test_cont_get_acl_no_perm(self):
        """
        JIRA ID: DAOS-3705

        Test Description: Test that container get-acl command doesn't
            get ACL information without permission.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,container,container_acl,daos_cmd
        :avocado: tags=GetContainerACLTest,test_cont_get_acl_no_perm
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
            self.container.uuid,
            outfile="outfile.txt")
        test_errs.extend(self.error_handling(self.daos_cmd.result, "-1001"))

        if test_errs:
            self.fail("container get-acl command expected to fail: \
                {}".format("\n".join(test_errs)))
