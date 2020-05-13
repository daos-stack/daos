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


class ContainerQueryAttributeTest(TestWithServers):
    """Test class for daos container query and attribute tests.

    Test Class Description:
        Query test: Create a pool, create a container, and call daos container
        query. From the output, verify the pool/container UUID matches the one
        that was returned when creating the pool/container.

        Attribute test:
        1. Create 5 attributes to a container. We can set attribute
        name and corresponding value, so set unique name-value pair for each of
        the 5 attributes.
        2. Call continaer list-attrs. This returns the 5 attribute names, so
        compare them against the actual names used.
        3. Call container get-attr. This returns the value of the corresponding
        name, so do this for each of the 5 attributes.

    :avocado: recursive
    """

    def test_container_query_attr(self):
        """JIRA ID: DAOS-4640

        Test Description:
            Test daos container query and attribute commands as described
            above.

        Use Cases:
            Test container query, set-attr, list-attr, and get-attr commands.

        :avocado: tags=all,pool,small,full_regression,cont_query_attr
        """
        # 1. Test pool query.
        kwargs = {"scm_size": "1G"}
        pool_create_result = self.get_dmg_command().get_output(
            "pool_create", **kwargs)
        expected_pool_uuid = pool_create_result[0]
        sr = pool_create_result[1]
        daos_cmd = DaosCommand(self.bin)
        # Create container and store the UUID as expected.
        kwargs = {"pool": expected_pool_uuid, "svc": sr}
        expected_cont_uuid = daos_cmd.get_output(
            "container_create", **kwargs)[0]
        # Call daos container query, obtain pool and container UUID, and
        # compare against those used when creating the pool and the container.
        kwargs["cont"] = expected_cont_uuid
        query_output = daos_cmd.get_output("container_query", **kwargs)[0]
        actual_pool_uuid = query_output[0]
        actual_cont_uuid = query_output[1]
        self.assertEqual(actual_pool_uuid, expected_pool_uuid)
        self.assertEqual(actual_cont_uuid, expected_cont_uuid)

        # 2. Test container set-attr, get-attr, and list-attrs.
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
            _ = daos_cmd.container_set_attr(
                pool=actual_pool_uuid, cont=actual_cont_uuid,
                attr=sample_attr, val=sample_val, svc=sr).stdout
            expected_attrs.append(sample_attr)
            expected_attrs_dict[sample_attr] = sample_val
        expected_attrs.sort()
        # List the attribute names.
        kwargs = {
            "pool": actual_pool_uuid,
            "svc": sr,
            "cont": actual_cont_uuid
        }
        actual_attrs = daos_cmd.get_output("container_list_attrs", **kwargs)
        actual_attrs.sort()
        self.assertEqual(actual_attrs, expected_attrs)
        # Verify each attribute's value.
        errors = []
        for i in range(5):
            kwargs["attr"] = sample_attrs[i]
            actual_val = daos_cmd.get_output("container_get_attr", **kwargs)[0]
            if sample_vals[i] != actual_val:
                errors.append("{} != {}".format(sample_vals[i], actual_val))
        self.assertEqual(len(errors), 0, '\n'.join(errors))
