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
        acl_file_name = os.path.join(self.tmp, "cont_test_owner_acl.txt")
        acl_entries = [
            secTestBase.acl_entry("user", self.current_user, "rwdaAtTo"),
        ]
        secTestBase.create_acl_file(acl_file_name, acl_entries)

        # Set up the pool and container.
        self.add_pool()
        self.container = self.create_container_with_daos(self.pool, acl_file=acl_file_name,
                                                         cont_type=cont_type)

    def _get_ownership(self):
        result = self.container.get_acl()
        return {
            "user": result['response']['owner_user'],
            "group": result['response']['owner_group'],
        }

    def _check_ownership(self, exp_user, exp_group):
        ownership = self._get_ownership()
        self.assertEqual(ownership["user"], exp_user, "user owner doesn't match")
        self.assertEqual(ownership["group"], exp_group, "group owner doesn't match")

    def test_container_set_owner_no_check_non_posix(self):
        """
        Description:
            Verify that daos container set-owner --no-check flag ignores missing user for non-POSIX
            container.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=security,container,cont_sec,cont_set_owner
        :avocado: tags=DaosContainerOwnerTest
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
        :avocado: tags=security,container,cont_sec,cont_set_owner
        :avocado: tags=DaosContainerOwnerTest
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

    def test_dmg_cont_set_owner(self):
        """
        Description:
            Verify dmg container set-owner

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=security,control,container,cont_sec,cont_set_owner
        :avocado: tags=DaosContainerOwnerTest,test_dmg_cont_set_owner
        """
        test_user = "fakeuser@"
        test_group = "fakegroup@"

        self._create_cont_with_acl(cont_type="python")
        orig_owner = self._get_ownership()

        # No user/group
        with self.dmg.no_exception():
            result = self.get_dmg_command().cont_set_owner(self.pool.identifier,
                                                           self.container.identifier)
        self.verify_daos_pool_cont_result(result, "set owner with no user or group", "fail",
                                          "at least one")
        self._check_ownership(orig_owner["user"], orig_owner["group"])  # expect unchanged

        # User only - not locally checked
        with self.dmg.no_exception():
            result = self.get_dmg_command().cont_set_owner(self.pool.identifier,
                                                           self.container.identifier,
                                                           user=test_user)
        self.verify_daos_pool_cont_result(result, "set owner user", "pass", None)
        self._check_ownership(test_user, orig_owner["group"])

        # Group only - not locally checked
        with self.dmg.no_exception():
            result = self.get_dmg_command().cont_set_owner(self.pool.identifier,
                                                           self.container.identifier,
                                                           group=test_group)
        self.verify_daos_pool_cont_result(result, "set owner group", "pass", None)
        self._check_ownership(test_user, test_group)

        # User and group
        with self.dmg.no_exception():
            result = self.get_dmg_command().cont_set_owner(self.pool.identifier,
                                                           self.container.identifier,
                                                           user="fakeuser2", group="fakegroup2")
        self.verify_daos_pool_cont_result(result, "set owner user and group", "pass", None)
        self._check_ownership("fakeuser2@", "fakegroup2@")

        # Labels as IDs
        with self.dmg.no_exception():
            result = self.get_dmg_command().cont_set_owner(self.pool.label.value,
                                                           self.container.label.value,
                                                           user=orig_owner["user"])
        self.verify_daos_pool_cont_result(result, "set owner user with labels", "pass", None)
        self._check_ownership(orig_owner["user"], "fakegroup2@")

        # UUIDs as IDs
        with self.dmg.no_exception():
            result = self.get_dmg_command().cont_set_owner(self.pool.uuid, self.container.uuid,
                                                           group=orig_owner["group"])
        self.verify_daos_pool_cont_result(result, "set owner group with UUIDs", "pass", None)
        self._check_ownership(orig_owner["user"], orig_owner["group"])
