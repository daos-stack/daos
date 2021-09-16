#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithServers


class QueryAttributeTest(TestWithServers):
    """Test class for daos pool query and attribute tests.

    Test Class Description:
        Query test: Create a pool and call daos pool query. From the output,
        verify the UUID matches the one that was returned when creating the
        pool. Also verify the pool size against the value that's passed in to
        dmg pool create.

        Attribute test:
        1. Create 5 attributes to a pool. We can set attribute
        name and corresponding value, so set unique name-value pair for each of
        the 5 attributes.
        2. Call pool list-attr. This returns the 5 attribute names, so compare
        them against the actual names used.
        3. Call pool get-attr. This returns the value of the corresponding
        name, so do this for each of the 5 attributes.

    :avocado: recursive
    """

    def test_query_attr(self):
        """JIRA ID: DAOS-4624

        Test Description:
            Test daos pool query and attribute commands as described above.

        Use Cases:
            Test query, set-attr, list-attr, and get-attr commands.

        :avocado: tags=all,full_regression
        :avocado: tags=small
        :avocado: tags=pool,pool_query_attr
        """
        errors = []
        daos_cmd = self.get_daos_command()

        # 1. Test pool query.
        expected_size = self.params.get("scm_size", "/run/pool/*")
        self.add_pool()

        # Call daos pool query, obtain pool UUID and SCM size, and compare
        # against those used when creating the pool.
        query_result = daos_cmd.pool_query(pool=self.pool.uuid)
        actual_uuid = query_result["response"]["uuid"]
        actual_size = query_result["response"]["tier_stats"][0]["total"]

        expected_uuid = self.pool.uuid.lower()
        if expected_uuid != actual_uuid:
            msg = "Unexpected UUID from daos pool query! " +\
                "Expected = {}; Actual = {}".format(expected_uuid, actual_uuid)
            errors.append(msg)

        if expected_size != actual_size:
            msg = "Unexpected Total Storage Tier 0 size from daos pool " +\
                "query! Expected = {}; Actual = {}".format(
                    expected_size, actual_size)
            errors.append(msg)

        # 2. Test pool set-attr, get-attr, and list-attrs.
        expected_attrs = []
        actual_attrs = []
        sample_attrs = []
        sample_vals = []

        # Create 5 attributes.
        for i in range(5):
            sample_attr = "attr" + str(i)
            sample_val = "val" + str(i)
            sample_attrs.append(sample_attr)
            sample_vals.append(sample_val)
            daos_cmd.pool_set_attr(
                pool=self.pool.uuid, attr=sample_attr, value=sample_val)
            expected_attrs.append(sample_attr)

        # List the attribute names and compare against those set.
        attrs = daos_cmd.pool_list_attrs(pool=self.pool.uuid)
        for attr in attrs["response"]:
            actual_attrs.append(attr["name"])

        actual_attrs.sort()
        expected_attrs.sort()

        if actual_attrs != expected_attrs:
            msg = "Unexpected attribute names! " +\
                "Expected = {}; Actual = {}".format(
                    expected_attrs, actual_attrs)
            errors.append(msg)

        # Get each attribute's value and compare against those set.
        for i in range(5):
            output = daos_cmd.pool_get_attr(
                pool=self.pool.uuid, attr=sample_attrs[i])
            actual_val = output["response"]["value"]
            if sample_vals[i] != actual_val:
                msg = "Unexpected attribute value! " +\
                    "Expected = {}; Actual = {}".format(
                        sample_vals[i], actual_val)
                errors.append(msg)

        if errors:
            self.fail("\n----- Errors detected! -----\n{}".format(
                "\n".join(errors)))
