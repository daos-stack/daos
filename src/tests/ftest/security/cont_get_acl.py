"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from avocado import fail_on
from cont_security_test_base import ContSecurityTestBase
from exception_utils import CommandFailure
from security_test_base import read_acl_file


class GetContainerACLTest(ContSecurityTestBase):
    """Test Class Description:

    Test to verify container ACL get command..

    :avocado: recursive
    """

    @fail_on(CommandFailure)
    def test_get_acl_valid(self):
        """JIRA ID: DAOS-3705

        Test Description: Test that container get-acl command performs as
            expected with valid inputs and verify that we can't overwrite
            an already existing file when using the --outfile argument.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,container,container_acl,daos_cmd
        :avocado: tags=GetContainerACLTest,test_get_acl_valid
        """
        self.pool = self.get_pool()
        self.container = self.get_container(self.pool)

        test_errs = []
        for verbose in [True, False]:
            for outfile in self.params.get("valid_out_filename", "/run/*"):
                path_to_file = os.path.join(
                    self.tmp, "{}_{}".format(outfile, verbose))

                # Disable raising an exception if the daos command fails
                with self.container.no_exception():
                    self.container.get_acl(verbose, path_to_file)

                # Verify consistency of acl obtained through the file
                file_acl = read_acl_file(path_to_file)
                self.acl_file_diff(self.container, file_acl)

                # Let's verify that we can't overwrite an already existing file
                # Disable raising an exception if the daos command fails
                with self.container.no_exception():
                    self.container.get_acl(verbose, path_to_file)
                test_errs.extend(
                    self.error_handling(self.container.daos.result, "file exists"))

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
        self.pool = self.get_pool()
        self.container = self.get_container(self.pool)

        # Let's give access to the pool to the root user
        self.pool.update_acl(False, entry="A::EVERYONE@:rw")

        # Let's check that we can't run as root (or other user) and get
        # acl information if no permissions are set for that user.
        # The root user shouldn't have access to getting container ACL entries
        with self.container.as_user('root'):
            # Disable raising an exception if the daos command fails
            with self.container.no_exception():
                self.container.get_acl(outfile="outfile.txt")
        test_errs = self.error_handling(self.container.daos.result, "-1001")

        if test_errs:
            self.fail("container get-acl command expected to fail: \
                {}".format("\n".join(test_errs)))
