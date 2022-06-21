#!/usr/bin/python3
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import security_test_base as secTestBase
from cont_security_test_base import ContSecurityTestBase
from pool_security_test_base import PoolSecurityTestBase


class DaosContainterSecurityTest(ContSecurityTestBase, PoolSecurityTestBase):
    # pylint: disable=too-few-public-methods,too-many-ancestors
    """Test daos_container user acls.

    :avocado: recursive
    """

    def test_container_user_acl(self):
        """
        Description:
            DAOS-4838: Verify container user security with ACL.
            DAOS-4390: Test daos_cont_set_owner
            DAOS-4839: Verify container group user with ACL.
            DAOS-4840: Verify container user and group access with
                       ACL grant/remove modification.
            DAOS-4841: Verify container ACL works when servers
                       not sync with client compute hosts.
        Test container 5 users enforcement order:
            (defined on test.yaml)
            OWNER: container owner assigned with the permissions.
            user:  container user assigned with the permissions.
            user-group: container user-group assigned with the permissions.
            GROUP: container group assigned with the permissions.
            EVERYONE: everyone assigned with the permissions.
        Test container user acl permissions:
            w  - set_container_attribute or data
            r  - get_container_attribute or data
            T  - set_container_property
            t  - get_container_property
            a  - get_container_acl_list
            A  - update_container_acl
            o  - set_container_owner
            d  - destroy_container
        Steps:
            (1)Setup
            (2)Create pool and container with acl
            (3)Verify container permissions rw, rw-attribute
            (4)Verify container permissions tT, rw-property
            (5)Verify container permissions aA, rw-acl
            (6)Verify container permission o, set-owner
            (7)Verify container permission d, delete
            (8)Cleanup

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=security,container_acl,cont_user_sec,cont_group_sec,cont_sec
        :avocado: tags=test_container_user_acl
        """

        #(1)Setup
        self.log.info("(1)==>Setup container user acl test.")
        cont_permission, expect_read, expect_write = self.params.get(
            "perm_expect", "/run/container_acl/permissions/*")
        new_test_user = self.params.get("new_user", "/run/container_acl/*")
        new_test_group = self.params.get("new_group", "/run/container_acl/*")
        attribute_name, attribute_value = self.params.get(
            "attribute", "/run/container_acl/*")
        property_name, property_value = self.params.get(
            "property", "/run/container_acl/*")
        secTestBase.add_del_user(
            self.hostlist_clients, "useradd", new_test_user)
        secTestBase.add_del_user(
            self.hostlist_clients, "groupadd", new_test_group)
        acl_file_name = os.path.join(
            self.tmp, self.params.get(
                "acl_file_name", "/run/container_acl/*", "cont_test_acl.txt"))
        test_user = self.params.get(
            "testuser", "/run/container_acl/daos_user/*")
        test_user_type = secTestBase.get_user_type(test_user)
        base_acl_entries = self.get_base_acl_entries(test_user)
        if test_user == "user":
            test_user = self.current_user
        if test_user == "group":
            test_user = self.current_group
        self.log.info(
            "==>(1.1)Start testing container acl on user: %s", test_user)

        #(2)Create pool and container with acl
        self.log.info("(2)==>Create a pool and a container with acl\n"
                      "   base_acl_entries= %s\n", base_acl_entries)
        self.add_pool()
        secTestBase.create_acl_file(acl_file_name, base_acl_entries)
        self.container_uuid = self.create_container_with_daos(
            self.pool, None, acl_file_name)

        #(3)Verify container permissions rw, rw-attribute
        permission_type = "attribute"
        self.log.info("(3)==>Verify container permission %s", permission_type)

        self.update_container_acl(
            secTestBase.acl_entry(test_user_type, test_user, "rw"))
        self.verify_cont_rw_attribute(
            "write", "pass", attribute_name, attribute_value)
        self.setup_container_acl_and_permission(
            test_user_type, test_user, permission_type, cont_permission)
        self.log.info(
            "(3.1)Verify container_attribute: write, expect: %s", expect_write)
        self.verify_cont_rw_attribute(
            "write", expect_write, attribute_name, attribute_value)
        self.log.info(
            "(3.2)Verify container_attribute: read, expect: %s", expect_read)
        self.verify_cont_rw_attribute("read", expect_read, attribute_name)

        #(4)Verify container permissions tT rw-property
        permission_type = "property"
        self.log.info("(4)==>Verify container permission tT, rw-property")
        self.log.info(
            "(4.1)Update container-acl %s, %s, permission_type: %s with %s",
            test_user_type, test_user, permission_type, cont_permission)
        self.setup_container_acl_and_permission(
            test_user_type, test_user, permission_type, cont_permission)
        self.log.info(
            "(4.2)Verify container_attribute: read, expect: %s", expect_read)
        self.verify_cont_rw_property("read", expect_read)
        self.log.info(
            "(4.3)Verify container_attribute: write, expect: %s", expect_write)
        self.verify_cont_rw_property(
            "write", expect_write, property_name, property_value)
        self.log.info(
            "(4.4)Verify container_attribute: read, expect: %s", expect_read)
        self.verify_cont_rw_property("read", expect_read)

        #(5)Verify container permissions aA, rw-acl
        permission_type = "acl"
        self.log.info("(5)==>Verify container permission aA, rw-acl ")
        self.log.info(
            "(5.1)Update container-acl %s, %s, permission_type: %s with %s",
            test_user_type, test_user, permission_type, cont_permission)
        expect = "pass"  #User who created the container has full acl access.
        self.setup_container_acl_and_permission(
            test_user_type, test_user, permission_type, cont_permission)
        self.log.info("(5.2)Verify container_acl: write, expect: %s", expect)
        self.verify_cont_rw_acl(
            "write", expect, secTestBase.acl_entry(
                test_user_type, test_user, cont_permission))
        self.log.info("(5.3)Verify container_acl: read, expect: %s", expect)
        self.verify_cont_rw_acl("read", expect)

        #(6)Verify container permission o, set-owner
        self.log.info("(6)==>Verify container permission o, set-owner")
        permission_type = "ownership"
        expect = "deny"
        if "w" in cont_permission:
            expect = "pass"
        self.log.info(
            "(6.1)Update container-set ownership %s, %s, permission_type:"
            " %s with %s", test_user_type, test_user, permission_type,
            cont_permission)
        self.setup_container_acl_and_permission(
            test_user_type, test_user, permission_type, cont_permission)
        self.log.info("(6.2)Verify container_ownership: write, expect: %s",
                      expect)
        self.verify_cont_set_owner(
            expect, new_test_user+"@", new_test_group+"@")

        #Verify container permission A acl-write after set container
        #  to a different owner.
        if cont_permission == "w":
            permission_type = "acl"
            expect = "deny"
            self.log.info("(6.3)Verify container_acl write after changed "
                          "ownership: expect: %s", expect)
            self.verify_cont_rw_acl("write", expect,
                                    secTestBase.acl_entry(
                                        test_user_type, test_user,
                                        cont_permission))

        #(7)Verify container permission d, delete
        self.log.info("(7)==>Verify cont-delete on container and pool"
                      " with/without d permission.")
        permission_type = "delete"
        c_permission = "rwaAtTod"
        p_permission = "rctd"
        expect = "pass"
        if "r" not in cont_permission:  #remove d from cont_permission
            c_permission = "rwaAtTo"
        if "w" not in cont_permission:  #remove d from pool_permission
            p_permission = "rct"
        if cont_permission == "":
            expect = "deny"
        self.update_container_acl(secTestBase.acl_entry(test_user_type,
                                                        test_user,
                                                        c_permission))
        self.update_pool_acl_entry(
            "update", secTestBase.acl_entry("user", "OWNER", p_permission))
        self.verify_cont_delete(expect)
        if expect == "pass": # Container deleted
            self.container = None

        #(8)Cleanup
        # Restore pool permissions in case they were altered
        self.update_pool_acl_entry(
            "update", secTestBase.acl_entry("user", "OWNER", "rctd"))
        secTestBase.add_del_user(
            self.hostlist_clients, "userdel", new_test_user)
        secTestBase.add_del_user(
            self.hostlist_clients, "groupdel", new_test_group)
