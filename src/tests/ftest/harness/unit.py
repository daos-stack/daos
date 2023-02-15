"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithoutServers
from data_utils import list_unique, list_flatten, dict_extract_values, dict_sub, dict_aggregate


class HarnessUnitTest(TestWithoutServers):
    """Harness unit tests for utilities.

    :avocado: recursive
    """

    def test_harness_unit_list_unique(self):
        """Verify list_unique().

        :avocado: tags=all
        :avocado: tags=harness
        :avocado: tags=HarnessUnitTest,test_harness_unit_list_unique
        """
        self.assertEqual(
            list_unique([1, 2, 3]),
            [1, 2, 3])
        self.assertEqual(
            list_unique([1, 2, 3, 3]),
            [1, 2, 3])
        self.assertEqual(
            list_unique([1, 2, {}]),
            [1, 2, {}])
        self.assertEqual(
            list_unique([{}, {}]),
            [{}])
        self.assertEqual(
            list_unique([{0: 1}, {2: 3}, {2: 3}]),
            [{0: 1}, {2: 3}])

    def test_harness_unit_list_flatten(self):
        """Verify list_flatten().

        :avocado: tags=all
        :avocado: tags=harness
        :avocado: tags=HarnessUnitTest,test_harness_unit_list_flatten
        """
        self.assertEqual(
            list_flatten([1, 2, 3]),
            [1, 2, 3])
        self.assertEqual(
            list_flatten([[1, 2], 3]),
            [1, 2, 3])
        self.assertEqual(
            list_flatten([[1, 2], [3]]),
            [1, 2, 3])
        self.assertEqual(
            list_flatten((1, 2, 3)),
            [1, 2, 3])
        self.assertEqual(
            list_flatten(((1, 2), 3)),
            [1, 2, 3])
        self.assertEqual(
            list_flatten(((1, 2), (3,))),
            [1, 2, 3])
        self.assertEqual(
            list_flatten([1, 2, 3, {'foo': 'bar'}]),
            [1, 2, 3, {'foo': 'bar'}])

    def test_harness_unit_dict_extract_values(self):
        """Verify dict_extract_values().

        :avocado: tags=all
        :avocado: tags=harness
        :avocado: tags=HarnessUnitTest,test_harness_unit_dict_extract_values
        """
        dict1 = {
            'key1': {
                'key1.1': {
                    'val1': 100,
                    'foo': 1000
                }
            },
            'key2': {
                'key2.1': {
                    'val1': 200
                },
                'foo': 2000
            }
        }
        self.assertEqual(
            dict_extract_values(dict1, ['val1']),
            [100, 200])
        self.assertEqual(
            dict_extract_values(dict1, ['*', 'val1']),
            [100, 200])
        self.assertEqual(
            dict_extract_values(dict1, ['*', '*', 'val1']),
            [100, 200])
        self.assertEqual(
            dict_extract_values(dict1, ['key1', '*', 'val1']),
            [100])
        self.assertEqual(
            dict_extract_values(dict1, ['*', 'key1.1', 'val1']),
            [100])
        self.assertEqual(
            dict_extract_values(dict1, ['key1', 'key1.1', 'val1']),
            [100])
        self.assertEqual(
            dict_extract_values(dict1, ['key1.1', 'val1']),
            [100])
        self.assertEqual(
            dict_extract_values(dict1, ['key1', 'val1']),
            [])
        self.assertEqual(
            dict_extract_values(dict1, ['foo']),
            [1000, 2000])

        dict2 = {
            'a': {
                'b': {
                    'a': 0
                }
            }
        }
        self.assertEqual(
            dict_extract_values(dict2, ['a']),
            [{'b': {'a': 0}}, 0])

    def test_harness_unit_dict_sub(self):
        """Verify dict_sub().

        :avocado: tags=all
        :avocado: tags=harness
        :avocado: tags=HarnessUnitTest,test_harness_unit_dict_sub
        """
        self.assertEqual(
            dict_sub(
                {
                    'key1': {
                        'key2': {
                            'val1': 1000,
                            'val2': 2000
                        }
                    }
                },
                {
                    'key1': {
                        'key2': {
                            'val1': 100,
                            'val2': 200
                        }
                    }
                }
            ),
            {
                'key1': {
                    'key2': {
                        'val1': 900,
                        'val2': 1800
                    }
                }
            })

    def test_harness_unit_dict_aggregate(self):
        """Verify dict_aggregate().

        :avocado: tags=all
        :avocado: tags=harness
        :avocado: tags=HarnessUnitTest,test_harness_unit_dict_aggregate
        """
        dict1 = {
            'key1': {
                'key1.1': {
                    'val1': 100,
                    'foo': 1000
                }
            },
            'key2': {
                'key2.1': {
                    'val1': 200
                },
                'foo': 2000
            }
        }
        self.assertEqual(
            dict_aggregate(dict1, ['val1']),
            {
                'mean': 150,
                'min': 100,
                'max': 200,
                'values': [100, 200]
            })
