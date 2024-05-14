"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

import security_test_base as secTestBase
from cont_security_test_base import ContSecurityTestBase
from pool_security_test_base import PoolSecurityTestBase


class DaosContainerOwnerTest(ContSecurityTestBase, PoolSecurityTestBase):
    # pylint: disable=too-few-public-methods,too-many-ancestors
    """Test daos_container user acls.

    :avocado: recursive
    """

    def _create_cont_with_acl(self, cont_type):
        # Set up an ACL that will allow us to reclaim the container
        acl_file_name = "cont_test_owner_acl.txt"
        acl_entries = [
            secTestBase.acl_entry("user", self.current_user, "rwdaAtTo"),
        ]
        secTestBase.create_acl_file(acl_file_name, acl_entries)

        # Set up the pool and container.
        self.add_pool()
        self.container = self.create_container_with_daos(self.pool, acl_file=acl_file_name,
                                                         cont_type=cont_type)

    def test_container_set_owner_no_check_non_posix(self):
        """
        Description:
            Verify that daos container set-owner --no-check flag ignores missing user for non-POSIX
            container.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=security,container,container_acl,cont_user_sec,cont_group_sec,cont_sec
        :avocado: tags=DaosContainerSecurityTest,test_container_set_owner_no_check
        :avocado: tags=test_container_set_owner_no_check_non_posix
        """
        fake_user = "fakeuser"
        fake_grp = "fakegroup"
        der_nonexist = '-1005'

        self._create_cont_with_acl(cont_type="python")

        # Attempt to change ownership to a fake user
        with self.container.no_exception():
            result = self.container.set_owner(user=fake_user, group=None)
        self.verify_daos_pool_cont_result(result, "set owner to fake user", "fail", der_nonexist)

        # Attempt to change ownership to fake group
        with self.container.no_exception():
            result = self.container.set_owner(user=None, group=fake_grp)
        self.verify_daos_pool_cont_result(result, "set owner to fake group", "fail", der_nonexist)

        # Using UID not allowed for non-POSIX
        with self.container.no_exception():
            result = self.container.set_owner(user=fake_user, uid=123, no_check=True)
        self.verify_daos_pool_cont_result(result, "set owner with uid", "fail",
                                          'for POSIX containers only')
        
        # Using GID not allowed for non-POSIX
        with self.container.no_exception():
            result = self.container.set_owner(group=fake_grp, gid=123, no_check=True)
        self.verify_daos_pool_cont_result(result, "set owner with gid", "fail",
                                          'for POSIX containers only')

        # Allow changing to fake user and group with no-check
        with self.container.no_exception():
            result = self.container.set_owner(user=fake_user, group=fake_grp, no_check=True)
        self.verify_daos_pool_cont_result(result, "set owner with no-check", "pass", None)

    def test_container_set_owner_no_check_posix(self):
        """
        Description:
            Verify that daos container set-owner --no-check flag works with uid/gid flags for
            POSIX containers

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=security,container,container_acl,cont_sec
        :avocado: tags=DaosContainerOwnerTest,test_container_set_owner_no_check
        :avocado: tags=test_container_set_owner_no_check_posix
        """
        fake_user = "fakeuser"
        fake_grp = "fakegroup"
        der_nonexist = '-1005'

        self._create_cont_with_acl(cont_type="posix")

        # Attempt to change ownership to a fake user
        with self.container.no_exception():
            result = self.container.set_owner(user=fake_user, group=None)
        self.verify_daos_pool_cont_result(result, "set owner to fake user", "fail", der_nonexist)

        # Attempt to change ownership to fake group
        with self.container.no_exception():
            result = self.container.set_owner(user=None, group=fake_grp)
        self.verify_daos_pool_cont_result(result, "set owner to fake group", "fail", der_nonexist)

        # No-check alone is not allowed for POSIX containers
        with self.container.no_exception():
            result = self.container.set_owner(user=fake_user, no_check=True)
        self.verify_daos_pool_cont_result(result, "set owner to user with no-check", "fail",
                                          'requires --uid')
        with self.container.no_exception():
            result = self.container.set_owner(group=fake_grp, no_check=True)
        self.verify_daos_pool_cont_result(result, "set owner to group with no-check", "fail",
                                          'requires --gid')

        # No-check flag missing, but uid/gid supplied
        with self.container.no_exception():
            result = self.container.set_owner(user=fake_user, uid=123, group=fake_grp, gid=456)
        self.verify_daos_pool_cont_result(result, "set owner with uid/gid without no-check", "fail", 
                                          '--no-check is required')

        # Supply new uid and gid with --no-check
        with self.container.no_exception():
            result = self.container.set_owner(user=fake_user, uid=123, group=fake_grp, gid=456,
                                              no_check=True)
        self.verify_daos_pool_cont_result(result, "set owner with uid/gid with no-check",
                                          "pass", None)
