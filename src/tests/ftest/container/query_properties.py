'''
  (C) Copyright 2018-2023 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from getpass import getuser
from grp import getgrgid
from os import getgid

from apricot import TestWithServers
from test_utils_container import DEFAULT_CONT_PROPS, add_container


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
        1. Create container 1 with default properties.
        2. Verify container 1 get-prop returns the same properties set on create.
        1. Create container 2 with some non-default properties related to checksum, type, etc.
        2. Verify container 2 get-prop returns the same properties set on create.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=QueryPropertiesTest,test_query_properties
        """
        self.log_step("Create pool")
        pool = self.get_pool()

        self.log_step("Create container 1 with default properties")
        container_1 = add_container(self, pool, "/run/container_1/*")

        self.log_step("Verify container 1 get-prop matches create")
        default_props = DEFAULT_CONT_PROPS.copy()
        default_props["label"] = container_1.label.value
        default_props["owner"] = f"{getuser()}@"
        default_props["group"] = f"{getgrgid(getgid()).gr_name}@"
        try:
            result = container_1.get_prop()
            container_1.validate_properties(result, default_props, ["root_oids"])
        except AssertionError:
            self.fail("Unexpected default properties from daos container 1 get-prop")

        self.log_step("Create container 2 with specific properties")
        container_2 = add_container(self, pool, "/run/container_2/*")

        expected_props = {
            "layout_type": self.params.get("layout_type", "/run/expected_get_prop/*"),
            "cksum": self.params.get("cksum", "/run/expected_get_prop/*"),
            "cksum_size": self.params.get("cksum_size", "/run/expected_get_prop/*"),
            "srv_cksum": self.params.get("srv_cksum", "/run/expected_get_prop/*")}

        self.log_step("Verify container 2 get-prop matches create")
        try:
            container_2.verify_prop(expected_props)
        except AssertionError:
            self.fail("Unexpected specific properties from daos container 2 get-prop")

        self.log.info("Test passed")
