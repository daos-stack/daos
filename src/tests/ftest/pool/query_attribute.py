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
  The Governments rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""
from apricot import TestWithServers
from daos_utils import DaosCommand


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

        :avocado: tags=all,pool,tiny,full_regression,pool_query_attr
        """
        # 1. Test pool query.
        # Use the same format as pool query.
        expected_size = "1000000000"
        expected = self.get_dmg_command().pool_create(scm_size=expected_size)
        daos_cmd = DaosCommand(self.bin)
        # Call daos pool query, obtain pool UUID and SCM size, and compare
        # against those used when creating the pool.
        kwargs = {"pool": expected["uuid"]}
        query_result = daos_cmd.get_output("pool_query", **kwargs)
        actual_uuid = query_result[0][0]
        actual_size = query_result[2][4]
        self.assertEqual(actual_uuid, expected["uuid"])
        self.assertEqual(actual_size, expected_size)

        # 2. Test pool set-attr, get-attr, and list-attrs.
        expected_attrs = []
        expected_attrs_dict = {}
        sample_attrs = []
        sample_vals = []
        # Create 5 attributes.
        for i in range(5):
            sample_attr = "attr" + str(i)
            sample_val = "val" + str(i)
            sample_attrs.append(sample_attr)
            sample_vals.append(sample_val)
            daos_cmd.pool_set_attr(
                pool=actual_uuid, attr=sample_attr, value=sample_val)
            expected_attrs.append(sample_attr)
            expected_attrs_dict[sample_attr] = sample_val
        # List the attribute names and compare against those set.
        kwargs = {"pool": actual_uuid}
        actual_attrs = daos_cmd.get_output("pool_list_attrs", **kwargs)
        actual_attrs.sort()
        expected_attrs.sort()
        self.assertEqual(actual_attrs, expected_attrs)
        # Get each attribute's value and compare against those set.
        for i in range(5):
            kwargs = {
                "pool": actual_uuid,
                "attr": sample_attrs[i],
            }
            actual_val = daos_cmd.get_output("pool_get_attr", **kwargs)[0]
            self.assertEqual(sample_vals[i], actual_val)
