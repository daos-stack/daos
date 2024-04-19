"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import base64

from apricot import TestWithServers
from general_utils import report_errors

# Test container set-attr, get-attr, and list-attrs with different
# types of characters.
# pylint: disable=anomalous-backslash-in-string
test_strings = [
    "abcd",
    "1234",
    "abc123",
    "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij",
    # Characters that don't require backslash. The backslashes in here
    # are required for the code to work, but not by daos.
    r"~@#$%^*-=_+[]\{\}/?.",
    # Characters that require backslash.
    r"\`\&\(\)\\\;\!\<\>\\\\\\,\\\\\\:",
    # Characters that include space.
    "\"aa bb\""]
# We added backslashes for the code to work, but get-attr output
# does not contain them, so prepare the expected output that does not
# include backslashes.
escape_to_not = {}
escape_to_not[test_strings[-3]] = '~@#$%^*-=_+[]{}/?.'
# We still need a backslash before the double quote for the code to
# work.
escape_to_not[test_strings[-2]] = r"`&()\;!<>\,\:"
escape_to_not[test_strings[-1]] = "aa bb"


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

    def test_container_query_attr(self):
        """JIRA ID: DAOS-4640

        Test Description:
            Test daos container query and attribute commands as described
            above.

        Use Cases:
            Test container query, set-attr, get-attr, and list-attrs.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container,daos_cmd
        :avocado: tags=ContainerQueryAttributeTest,test_container_query_attr
        """
        # Create a pool and a container.
        pool = self.get_pool()
        container = self.get_container(pool)

        # Call daos container query, obtain pool and container UUID, and
        # compare against those used when creating the pool and the container.
        data = container.query()['response']
        actual_pool_uuid = data['pool_uuid']
        actual_cont_uuid = data['container_uuid']
        self.assertEqual(
            actual_pool_uuid, pool.uuid.lower(),
            'pool UUID from cont query does not match pool create')
        self.assertEqual(
            actual_cont_uuid, container.uuid.lower(),
            'container UUID from cont query does not match cont create')

        # Prepare attr-value pairs. Use the test_strings in value for the first
        # 7 and in attr for the next 7.
        attr_values = []
        attr_idx = 0
        for idx in range(2):
            for test_string in test_strings:
                if idx == 0:
                    attr_values.append(["attr" + str(attr_idx), test_string])
                else:
                    attr_values.append([test_string, "attr" + str(attr_idx)])
                attr_idx += 1

        # Set and verify get-attr.
        errors = []
        expected_attrs = []

        for attr_value in attr_values:
            container.set_attr(attrs={attr_value[0]: attr_value[1]})

            data = container.get_attr(attr_value[0])['response']

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

        report_errors(self, errors)

        # Verify that attr-lists works with test_strings.
        expected_attrs.sort()
        actual_attrs = sorted(list(container.list_attrs()['response']))
        self.assertEqual(actual_attrs, expected_attrs, 'list-attrs does not match set-attr')

    def test_container_query_attrs(self):
        """JIRA ID: DAOS-4640

        Test Description:
            Test daos container query and attribute commands as described
            above.

        Use Cases:
            Test container query, set-attr, get-attr with bulk inputs.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container,daos_cmd
        :avocado: tags=ContainerQueryAttributeTest,test_container_query_attrs
        """
        # Create a pool and a container.
        pool = self.get_pool()
        container = self.get_container(pool)

        # Call daos container query, obtain pool and container UUID, and
        # compare against those used when creating the pool and the container.
        data = container.query()['response']
        actual_pool_uuid = data['pool_uuid']
        actual_cont_uuid = data['container_uuid']
        self.assertEqual(
            actual_pool_uuid, pool.uuid.lower(),
            'pool UUID from cont query does not match pool create')
        self.assertEqual(
            actual_cont_uuid, container.uuid.lower(),
            'container UUID from cont query does not match cont create')

        # Prepare attr-value pairs. Use the test_strings in value for the first
        # 7 and in attr for the next 7.
        attr_values = {}
        attr_idx = 0
        for idx in range(2):
            for test_string in test_strings:
                if idx == 0:
                    attr_values["attr" + str(attr_idx)] = test_string
                else:
                    attr_values[test_string] = "attr" + str(attr_idx)
                attr_idx += 1

        # Set and verify get-attr.
        errors = []

        # bulk-set all attributes
        container.set_attr(attrs=attr_values)

        # bulk-get all attributes
        data = container.get_attr(list(attr_values))['response']

        for attr_resp in data:
            key = attr_resp["name"]
            for esc_key, val in escape_to_not.items():
                if val == key:
                    key = esc_key
                    break

            actual_val = base64.b64decode(attr_resp["value"]).decode()
            if attr_values[key] in escape_to_not:
                # Special character string.
                if actual_val != escape_to_not[attr_values[key]]:
                    errors.append(
                        "Unexpected output for get_attr: {} != {}\n".format(
                            actual_val, escape_to_not[attr_values[key]]))
            else:
                # Standard character string.
                if actual_val != attr_values[key]:
                    errors.append(
                        "Unexpected output for get_attr: {} != {}\n".format(
                            actual_val, attr_values[key]))

        report_errors(self, errors)

    def test_list_attrs_long(self):
        """JIRA ID: DAOS-4640

        Test Description:
            Set many attributes and verify list-attrs works.

        Use Cases:
            Test daos container list-attrs with 50 attributes.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container,daos_cmd
        :avocado: tags=ContainerQueryAttributeTest,test_list_attrs_long
        """
        # Create a pool and a container.
        pool = self.get_pool()
        container = self.get_container(pool)

        expected_attrs = {"attr" + str(idx): "val" + str(idx) for idx in range(50)}

        container.set_attr(attrs=expected_attrs)

        actual_attr_names = sorted(list(container.list_attrs()['response']))
        expected_attr_names = sorted(expected_attrs.keys())
        self.assertEqual(
            actual_attr_names, expected_attr_names, "Unexpected output from list_attrs")
