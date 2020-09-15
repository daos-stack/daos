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

from cont_security_test_base import ContSecurityTestBase
from command_utils import CommandFailure
from avocado.utils import process
from avocado import fail_on


class DeleteContainerACLTest(ContSecurityTestBase):
    # pylint: disable=too-many-ancestors
    """Test Class Description:

    Test to verify ACL entry deletion.

    :avocado: recursive
    """
    def setUp(self):
        """Set up each test case."""
        super(DeleteContainerACLTest, self).setUp()
        self.daos_cmd = self.get_daos_command()
        self.prepare_pool()
        self.add_container(self.pool)

        # Get list of ACL entries
        cont_acl = self.get_container_acl_list(
            self.pool.uuid, self.pool.svc_ranks[0], self.container.uuid)

        # Get principals
        self.principals_table = {}
        for entry in cont_acl:
            self.principals_table[entry.split(":")[2]] = entry

    def test_acl_delete_invalid_inputs(self):
        """
        JIRA ID: DAOS-3714

        Test Description: Test that container delete command performs as
            expected with invalid inputs.

        :avocado: tags=all,pr,security,container_acl,cont_delete_acl
        """
        # Get list of invalid ACL principal values
        invalid_principals = self.params.get("invalid_principals", "/run/*")

        # Check for failure on invalid inputs.
        for principal in invalid_principals:
            try:
                self.daos_cmd.container_delete_acl(
                    self.pool.uuid,
                    self.pool.svc_ranks[0],
                    self.container.uuid,
                    principal)
            except process.CmdError as err:
                if "-1003" in err.result.stderr:
                    self.log.info(
                        "Found expected error %s with invalid principal %s",
                        err.result.stderr,
                        principal)
                else:
                    self.fail("delete-acl seems to have failed with \
                        unexpected error: {}".format(err))
        self.fail("container delete-acl command expected to fail.")

    @fail_on(CommandFailure)
    def test_delete_valid_acl(self):
        """
        JIRA ID: DAOS-3714

        Test Description: Test that container delete command successfully
            removes principal in ACL.

        :avocado: tags=all,pr,security,container_acl,cont_delete_acl
        """
        for principal in self.principals_table:
            self.daos_cmd.container_delete_acl(
                self.pool.uuid,
                self.pool.svc_ranks[0],
                self.container.uuid,
                principal)
            if self.principals_table[principal] in self.daos_cmd.result.stdout:
                self.fail(
                    "Found acl that was to be deleted in output: {}".format(
                        self.daos_cmd.result.stdout))

    def test_no_user_permissions(self):
        """
        JIRA ID: DAOS-3714

        Test Description: Test that container delete command successfully
            removes principal in ACL.

        :avocado: tags=all,pr,security,container_acl,cont_delete_acl
        """
        # Let's give access to the pool to the root user
        self.get_dmg_command().pool_update_acl(
            self.pool.uuid, entry="A::EVERYONE@:rw")

        # The root user shouldn't have access to deleting container ACL entries
        self.daos_cmd.sudo = True

        # Let's check that we can't run as root (or other user) and delete
        # entries if no permissions are set for that user.

        for principal in self.principals_table:
            try:
                self.daos_cmd.container_delete_acl(
                    self.pool.uuid,
                    self.pool.svc_ranks[0],
                    self.container.uuid,
                    principal)
            except process.CmdError as err:
                if "-1001" in err.result.stderr:
                    self.log.info(
                        "Found expected error %s with invalid principal %s",
                        err.result.stderr, principal)
                else:
                    self.fail("delete-acl seems to have failed with \
                        unexpected error: {}".format(err))
        self.fail("container delete-acl command expected to fail.")
