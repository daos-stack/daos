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

        :avocado: tags=all,full_regression,security,container_acl,cont_user_sec
        """

        #(1)Setup
        self.log.info("(1)==>Setup container user acl test.")
        base_acl_entries = [
            "",
            secTestBase.acl_entry("user", self.current_user, ""),
            secTestBase.acl_entry("group", "GROUP", ""),
            secTestBase.acl_entry("group", self.current_group, ""),
            secTestBase.acl_entry("user", "EVERYONE", "")]
        user_type = self.params.get("user_type",
                                    "/run/container_acl/*", "user")
        cont_permission, expect_read, expect_write = self.params.get(
            "perm_expect", "/run/container_acl/permissions/*")
        test_user = self.params.get("new_user", "/run/container_acl/*")
        test_group = self.params.get("new_group", "/run/container_acl/*")
        attribute_name, attribute_value = self.params.get(
            "attribute", "/run/container_acl/*")
        property_name, property_value = self.params.get(
            "property", "/run/container_acl/*")
        secTestBase.add_del_user(self.hostlist_clients, "useradd", test_user)
        secTestBase.add_del_user(self.hostlist_clients, "groupadd", test_group)
        acl_file_name = os.path.join(
            self.tmp, self.params.get(
                "acl_file_name", "/run/container_acl/*", "cont_test_acl.txt"))

        #(2)Create pool and container with acl
        self.log.info("(2)==>Create a pool and a container with acl\n"
                      "   base_acl_entries= %s\n", base_acl_entries)
        self.pool_uuid, self.pool_svc = self.create_pool_with_dmg()
        secTestBase.create_acl_file(acl_file_name, base_acl_entries)
        self.container_uuid = self.create_container_with_daos(
            self.pool, None, acl_file_name)

        #(3)Verify container permissions rw, rw-attribute
        permission_type = "attribute"
        self.log.info("(3)==>Verify container permission %s", permission_type)
        self.update_container_acl(
            secTestBase.acl_entry(user_type, self.current_user, "rw"))
        self.verify_cont_rw_attribute(
            "write", "pass", attribute_name, attribute_value)
        self.setup_container_acl_and_permission(
            user_type, self.current_user, permission_type, cont_permission)
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
            user_type, self.current_user, permission_type, cont_permission)
        self.setup_container_acl_and_permission(
            user_type, self.current_user, permission_type, cont_permission)
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
            user_type, self.current_user, permission_type, cont_permission)
        expect = "pass"  #User who created the container has full acl access.
        self.setup_container_acl_and_permission(
            user_type, self.current_user, permission_type, cont_permission)
        self.log.info("(5.2)Verify container_acl: write, expect: %s", expect)
        self.verify_cont_rw_acl(
            "write", expect, secTestBase.acl_entry(
                user_type, self.current_user, cont_permission))
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
            " %s with %s", user_type, self.current_user, permission_type,
            cont_permission)
        self.setup_container_acl_and_permission(
            user_type, self.current_user, permission_type, cont_permission)
        self.log.info("(6.2)Verify container_ownership: write, expect: %s",
                      expect)
        self.verify_cont_set_owner(expect, test_user+"@", test_group+"@")
        self.log.info("(6.3)Verify container_ownership: read, expect: %s",
                      expect)
        self.verify_cont_rw_property("read", expect)

        #Verify container permission A acl-write after set container
        #  to a different owner.
        if cont_permission == "w":
            permission_type = "acl"
            expect = "deny"
            self.log.info("(6.4)Verify container_acl write after changed "
                          "ownership: expect: %s", expect)
            self.verify_cont_rw_acl("write", expect,
                                    secTestBase.acl_entry(
                                        user_type, self.current_user,
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
        self.update_container_acl(secTestBase.acl_entry(user_type,
                                                        self.current_user,
                                                        c_permission))
        self.update_pool_acl_entry(self.pool_uuid,
                                   "update",
                                   secTestBase.acl_entry(user_type,
                                                         "OWNER",
                                                         p_permission))
        self.verify_cont_delete(expect)

        #(8)Cleanup
        secTestBase.add_del_user(self.hostlist_clients, "userdel", test_user)
        secTestBase.add_del_user(
            self.hostlist_clients, "groupdel", test_group)
