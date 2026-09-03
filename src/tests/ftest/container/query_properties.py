'''
  (C) Copyright 2018-2023 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import TestWithServers
from test_utils_container import DEFAULT_CONT_PROPS, add_container
from test_utils_pool import add_pool


class QueryPropertiesTest(TestWithServers):
    """
    Test Class Description: Verify daos container get-prop

    :avocado: recursive
    """

    def test_query_properties(self):
        """JIRA ID: DAOS-9515

        Test Description: Verify container properties are correctly set by configuring
        some properties during create.

        Use Cases:
        1. Create a container with some properties related to checksum, type, etc. configured.
        2. Verify container get-prop returns the same properties set on create.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=QueryPropertiesTest,test_query_properties
        """
        self.log_step("Create pool")
        pool = add_pool(self)
        containers = []

        self.log_step("Create a container with default properties")
        containers.append(add_container(self, pool, "/run/container_1/*"))

        self.log_step("Verify container get-prop matches create")
        if not containers[-1].verify_prop(DEFAULT_CONT_PROPS, exclusive=False):
            self.fail("Unexpected default properties from daos container get-prop")

        self.log_step("Create a container with specific properties")
        containers.append(add_container(self, pool, "/run/container_2/*"))

        expected_props = {
            "layout_type": self.params.get("layout_type", "/run/expected_get_prop/*"),
            "cksum": self.params.get("cksum", "/run/expected_get_prop/*"),
            "cksum_size": self.params.get("cksum_size", "/run/expected_get_prop/*"),
            "srv_cksum": self.params.get("srv_cksum", "/run/expected_get_prop/*")}

        self.log_step("Verify container get-prop matches create")
        if not containers[-1].verify_prop(expected_props, exclusive=True):
            self.fail("Unexpected specific properties from daos container get-prop")

        self.log.info("Test passed")
