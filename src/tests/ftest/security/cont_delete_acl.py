"""
  (C) Copyright 2020-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado import fail_on
from cont_security_test_base import ContSecurityTestBase
from exception_utils import CommandFailure


class DeleteContainerACLTest(ContSecurityTestBase):
    """Test Class Description:

    Test to verify ACL entry deletion.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        super().setUp()
        self.pool = self.get_pool()
        self.container = self.get_container(self.pool)
        # Get list of ACL entries
        cont_acl = self.get_container_acl_list(self.container)

        # Get principals
        self.principals_table = {}
        for entry in cont_acl:
            self.principals_table[entry.split(":")[2]] = entry

    def test_acl_delete_invalid_inputs(self):
        """
        JIRA ID: DAOS-3714

        Test Description: Test that container delete command performs as
            expected with invalid inputs.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,container,container_acl,daos_cmd
        :avocado: tags=DeleteContainerACLTest,test_acl_delete_invalid_inputs
        """
        # Get list of invalid ACL principal values
        invalid_principals = self.params.get("invalid_principals", "/run/*")

        # Check for failure on invalid inputs.
        test_errs = []
        for principal in invalid_principals:
            # Disable raising an exception if the daos command fails
            with self.container.no_exception():
                result = self.container.delete_acl(principal)
            test_errs.extend(self.error_handling(result, "-1003"))
        if test_errs:
            self.fail("container delete-acl command expected to fail: \
                {}".format("\n".join(test_errs)))

    @fail_on(CommandFailure)
    def test_delete_valid_acl(self):
        """
        JIRA ID: DAOS-3714

        Test Description: Test that container delete command successfully
            removes principal in ACL.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,container,container_acl,daos_cmd
        :avocado: tags=DeleteContainerACLTest,test_delete_valid_acl
        """
        for principal, entry in self.principals_table.items():
            result = self.container.delete_acl(principal)
            if entry in result.stdout_text:
                self.fail(
                    "Found acl that was to be deleted in output: {}".format(
                        result.stdout_text))

    def test_cont_delete_acl_no_perm(self):
        """
        JIRA ID: DAOS-3714

        Test Description: Test that container delete command doesn't
            remove principal in ACL without permission.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=security,container,container_acl,daos_cmd
        :avocado: tags=DeleteContainerACLTest,test_cont_delete_acl_no_perm
        """
        # Let's give access to the pool to the root user
        self.pool.update_acl(False, entry="A::EVERYONE@:rw")

        # Let's check that we can't run as root (or other user) and delete
        # entries if no permissions are set for that user.
        test_errs = []
        for principal in self.principals_table:
            # The root user shouldn't have access to deleting container ACL entries
            with self.container.as_user('root'):
                # Disable raising an exception if the daos command fails
                with self.container.no_exception():
                    result = self.container.delete_acl(principal)
            test_errs.extend(self.error_handling(result, "-1001"))
        if test_errs:
            self.fail("container delete-acl command expected to fail: \
                {}".format("\n".join(test_errs)))
