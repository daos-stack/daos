#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import base64
from apricot import TestWithServers


class ContainerQueryAttributeTest(TestWithServers):
    # pylint: disable=anomalous-backslash-in-string
    """Test class for daos container query and attribute tests.

    Test Class Description:
        Query test: Create a pool, create a container, and call daos container
        query. From the output, verify the pool/container UUID matches the one
        that was returned when creating the pool/container.

        Attribute test:
        1. Prepare 7 types of strings; alphabets, numbers, special characters,
        etc.
        2. Create attributes with each of these 7 types in attr and value;
        i.e., 14 total attributes are created.
        3. Call get-attr for each of the 14 attrs and verify the returned
        values.
        4. Call list-attrs and verify the returned attrs.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a ContainerQueryAttribute object."""
        super().__init__(*args, **kwargs)
        self.expected_cont_uuid = None
        self.daos_cmd = None

    def test_container_query_attr(self):
        """JIRA ID: DAOS-4640

        Test Description:
            Test daos container query and attribute commands as described
            above.

        Use Cases:
            Test container query, set-attr, get-attr, and list-attrs.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=cont_query_attr,test_container_query_attr
        """
        # Create a pool and a container.
        self.add_pool()
        self.add_container(pool=self.pool)

        self.daos_cmd = self.get_daos_command()

        # Call daos container query, obtain pool and container UUID, and
        # compare against those used when creating the pool and the container.
        kwargs = {
            "pool": self.pool.uuid,
            "cont": self.container.uuid
        }
        data = self.daos_cmd.container_query(**kwargs)['response']
        actual_pool_uuid = data['pool_uuid']
        actual_cont_uuid = data['container_uuid']
        self.assertEqual(actual_pool_uuid, self.pool.uuid.lower())
        self.assertEqual(actual_cont_uuid, self.container.uuid.lower())

        # Test container set-attr, get-attr, and list-attrs with different
        # types of characters.
        test_strings = [
            "abcd",
            "1234",
            "abc123",
            "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij",
            # Characters that don't require backslash. The backslashes in here
            # are required for the code to work, but not by daos.
            "~@#$%^*-=_+[]\{\}:/?,.",  # noqa: W605
            # Characters that require backslash.
            "\`\&\(\)\\\;\\'\\\"\!\<\>",  # noqa: W605
            # Characters that include space.
            "\"aa bb\""]
        # We added backslashes for the code to work, but get-attr output
        # does not contain them, so prepare the expected output that does not
        # include backslashes.
        escape_to_not = {}
        escape_to_not[test_strings[-3]] = "~@#$%^*-=_+[]{}:/?,."
        # We still need a backslash before the double quote for the code to
        # work.
        escape_to_not[test_strings[-2]] = "`&()\;'\"!<>"  # noqa: W605
        escape_to_not[test_strings[-1]] = "aa bb"
        # Prepare attr-value pairs. Use the test_strings in value for the first
        # 7 and in attr for the next 7.
        attr_values = []
        j = 0
        for i in range(2):
            for test_string in test_strings:
                if i == 0:
                    attr_values.append(["attr" + str(j), test_string])
                else:
                    attr_values.append([test_string, "attr" + str(j)])
                j += 1

        # Set and verify get-attr.
        errors = []
        expected_attrs = []

        for attr_value in attr_values:
            self.daos_cmd.container_set_attr(
                pool=actual_pool_uuid, cont=actual_cont_uuid,
                attr=attr_value[0], val=attr_value[1])

            kwargs["attr"] = attr_value[0]
            data = self.daos_cmd.container_get_attr(**kwargs)['response']

            actual_val = base64.b64decode(data["value"]).decode()
            if attr_value[1] in escape_to_not:
                # Special character string.
                if actual_val != escape_to_not[attr_value[1]]:
                    errors.append(
                        "Unexpected output for get_attr: {} != {}\n".format(
                            actual_val, escape_to_not[attr_value[1]]))
            else:
                # Standard character string.
                if actual_val != attr_value[1]:
                    errors.append(
                        "Unexpected output for get_attr: {} != {}\n".format(
                            actual_val, attr_value[1]))
            # Collect comparable attr as a preparation of list-attrs test.
            if attr_value[0] in escape_to_not:
                expected_attrs.append(escape_to_not[attr_value[0]])
            else:
                expected_attrs.append(attr_value[0])

        self.assertEqual(len(errors), 0, "; ".join(errors))

        # Verify that attr-lists works with test_strings.
        expected_attrs.sort()
        kwargs = {
            "pool": actual_pool_uuid,
            "cont": actual_cont_uuid
        }
        data = self.daos_cmd.container_list_attrs(**kwargs)['response']
        actual_attrs = list(data)
        actual_attrs.sort()
        self.log.debug(str(actual_attrs))
        self.assertEqual(actual_attrs, expected_attrs)

    def test_list_attrs_long(self):
        """JIRA ID: DAOS-4640

        Test Description:
            Set many attributes and verify list-attrs works.

        Use Cases:
            Test daos container list-attrs with 50 attributes.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=cont_list_attrs,test_list_attrs_long
        """
        # Create a pool and a container.
        self.add_pool()
        self.add_container(pool=self.pool)

        self.daos_cmd = self.get_daos_command()

        expected_attrs = []
        vals = []

        for i in range(50):
            expected_attrs.append("attr" + str(i))
            vals.append("val" + str(i))

        for expected_attr, val in zip(expected_attrs, vals):
            _ = self.daos_cmd.container_set_attr(
                pool=self.pool.uuid, cont=self.container.uuid,
                attr=expected_attr, val=val)

        expected_attrs.sort()

        kwargs = {
            "pool": self.pool.uuid,
            "cont": self.container.uuid
        }
        data = self.daos_cmd.container_list_attrs(**kwargs)['response']
        actual_attrs = list(data)
        actual_attrs.sort()
        self.assertEqual(
            expected_attrs, actual_attrs, "Unexpected output from list_attrs")
