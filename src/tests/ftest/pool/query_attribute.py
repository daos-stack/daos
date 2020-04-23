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
from dmg_utils import get_pool_uuid_service_replicas_from_stdout
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
        stdoutput = self.get_dmg_command().pool_create(
            scm_size=expected_size).stdout
        expected_uuid, service_replicas = \
            get_pool_uuid_service_replicas_from_stdout(stdoutput)
        daos_cmd = DaosCommand(self.bin)
        # Call daos pool query, obtain pool UUID and SCM size, and compare
        # against those used when creating the pool.
        query_stdout = daos_cmd.pool_query(
            pool=expected_uuid, svc=service_replicas).stdout
        actual_uuid = daos_cmd.get_pool_uuid(query_stdout)
        # First Total size is the SCM size.
        actual_size = daos_cmd.get_sizes(query_stdout)[0]
        self.assertEqual(actual_uuid, expected_uuid)
        self.assertEqual(actual_size, expected_size)

        # 2. Test pool set-attr, get-attr, and list-attrs.
        expected_attrs = set()
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
                pool=actual_uuid, attr=sample_attr, value=sample_val,
                svc=service_replicas).stdout
            expected_attrs.add(sample_attr)
            expected_attrs_dict[sample_attr] = sample_val
        # List the attribute names.
        stdoutput = daos_cmd.pool_list_attrs(
            pool=actual_uuid, svc=service_replicas).stdout
        actual_attrs = set()
        # Sample list output.
        # 04/19-21:16:31.62 wolf-3 Pool attributes:
        # 04/19-21:16:31.62 wolf-3 attr0
        # 04/19-21:16:31.62 wolf-3 attr1
        # 04/19-21:16:31.62 wolf-3 attr4
        # 04/19-21:16:31.62 wolf-3 attr3
        # 04/19-21:16:31.62 wolf-3 attr2
        lines = stdoutput.splitlines()
        for i in range(1, len(lines)):
            actual_attrs.add(lines[i].split()[2])
        self.assertEqual(actual_attrs, expected_attrs)
        # Get each attribute's value.
        actual_attrs_dict = {}
        for i in range(5):
            # Sample get-attr output - no line break.
            # 04/19-21:16:32.66 wolf-3 Pool's attr2 attribute value:
            # 04/19-21:16:32.66 wolf-3 val2
            stdoutput = daos_cmd.pool_get_attr(
                pool=actual_uuid, attr=sample_attrs[i],
                svc=service_replicas).stdout
            actual_val = stdoutput.split()[-1]
            self.assertEqual(sample_vals[i], actual_val)
