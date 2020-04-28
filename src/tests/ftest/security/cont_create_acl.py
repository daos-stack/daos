#!/usr/bin/python
"""
  (C) Copyright 2018-2020 Intel Corporation.

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
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from cont_security_test_base import ContSecurityTestBase

# pylint: disable=too-many-ancestors
class CreateContainterACLTest(ContSecurityTestBase):
    # pylint: disable=too-few-public-methods
    """Tests container basics including create, destroy, open, query and close.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a CreateContainterACLTest object."""
        super(CreateContainterACLTest, self).__init__(*args, **kwargs)

    def test_container_basics(self):
        """Test basic container create/destroy/open/close/query.

            1. Create a pool (dmg tool) with no acl file passed.
            2. Create a container (daos tool) with no acl file pased.
            3. Destroy the container.
            4. Create a container (daos tool) with a valid acl file passed.
            5. Destroy the container.
            6. Create a container (daos tool) with an invalid acl file passed.
            7. Destroy the pool.
            8. Remove all files created

        # pylint: disable=line-too-long
        :avocado: tags=all,full_regression,security,container_acl,cont_create_acl
        """
        ## Create a pool
        self.log.info("===> Creating a pool with no ACL file passed")
        self.create_pool_with_dmg()

        ## Create a container with no ACL file passed
        self.log.info("===> Creating a container with no ACL file passed")
        self.create_container_with_daos()

        ## Destroy the container
        self.log.info("===> Destroying the container")
        self.destroy_container_with_daos()

        ## Create a valid ACL file
        self.log.info("===> Generating a valid ACL file")
        self.generate_acl_file("valid")

        ## Create a container with a valid ACL file passed
        self.log.info("===> Creating a container with an ACL file passed")
        self.create_container_with_daos("valid")

        ## Destroy the container
        self.log.info("===> Destroying the container")
        self.destroy_container_with_daos()

        ## Create an invalid ACL file
        self.log.info("===> Generating an invalid ACL file")
        self.generate_acl_file("invalid")

        ## Create a container with an invalid ACL file passed
        self.log.info("===> Creating a container with invalid ACL file passed")
        self.create_container_with_daos("invalid")

        ## Destroy the pool
        self.log.info("===> Destroying the pool")
        self.destroy_pool_with_dmg()

        ## Cleanup environment
        self.log.info("===> Cleaning the environment")
        types = ["valid", "invalid"]
        self.cleanup(types)
