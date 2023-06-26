"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ClusterShell.NodeSet import NodeSet

from apricot import TestWithoutServers
from data_utils import list_unique, list_flatten, list_stats, \
    dict_extract_values, dict_subtract
from host_utils import get_local_host
from run_utils import run_remote


class HarnessUnitTest(TestWithoutServers):
    """Harness unit tests for utilities.

    :avocado: recursive
    """

    def test_harness_unit_list_unique(self):
        """Verify list_unique().

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,dict_utils
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
        :avocado: tags=vm
        :avocado: tags=harness,dict_utils
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

    def test_harness_unit_list_stats(self):
        """Verify list_stats().

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,dict_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_list_stats
        """
        self.assertEqual(
            list_stats([100, 200]),
            {
                'mean': 150,
                'min': 100,
                'max': 200
            })
        self.assertEqual(
            list_stats([-100, 200]),
            {
                'mean': 50,
                'min': -100,
                'max': 200
            })

    def test_harness_unit_dict_extract_values(self):
        """Verify dict_extract_values().

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,dict_utils
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

    def test_harness_unit_dict_subtract(self):
        """Verify dict_subtract().

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,dict_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_dict_subtract
        """
        dict1 = {
            'key1': {
                'key2': {
                    'val1': 1000,
                    'val2': 2000
                }
            }
        }
        dict2 = {
            'key1': {
                'key2': {
                    'val1': 100,
                    'val2': 200
                }
            }
        }
        self.assertEqual(
            dict_subtract(dict1, dict2),
            {
                'key1': {
                    'key2': {
                        'val1': 900,
                        'val2': 1800
                    }
                }
            })

    def test_harness_unit_run_remote(self):
        """Verify run_remote().

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_remote
        """
        host = get_local_host()
        command = 'echo stdout; echo stderr 1>&2'

        self.log_step('Running command w/ combined stdout and stderr')
        result = run_remote(self.log, host, command, stderr=False)
        self.assertTrue(result.passed, 'Command failed; expected to pass')
        self.assertEqual(1, len(result.output), 'Incorrect number of unique command outputs')
        self.assertEqual(command, result.output[0].command, 'Incorrect ResultData.command')
        self.assertEqual(0, result.output[0].returncode, 'Incorrect ResultData.returncode')
        self.assertEqual(host, result.output[0].hosts, 'Incorrect ResultData.hosts')
        self.assertEqual(
            ['stdout', 'stderr'], result.output[0].stdout, 'Incorrect ResultData.stdout')
        self.assertEqual([], result.output[0].stderr, 'Incorrect ResultData.stderr')
        self.assertEqual(False, result.output[0].timeout, 'Incorrect ResultData.timeout')
        self.assertEqual(True, result.output[0].homogeneous, 'Incorrect ResultData.homogeneous')
        self.assertEqual(True, result.output[0].passed, 'Incorrect ResultData.passed')
        self.assertEqual(host, result.output[0].passed_hosts, 'Incorrect ResultData.passed_hosts')
        self.assertEqual(
            NodeSet(), result.output[0].failed_hosts, 'Incorrect ResultData.failed_hosts')
        self.assertEqual(
            {str(host): 'stdout\nstderr'}, result.output[0].all_stdout,
            'Incorrect ResultData.all_stdout')
        self.assertEqual(
            {str(host): ''}, result.output[0].all_stderr, 'Incorrect ResultData.all_stderr')

        self.log_step('Running command w/ separated stdout and stderr')
        result = run_remote(self.log, host, command, stderr=True)
        self.assertTrue(result.passed, 'Command failed; expected to pass')
        self.assertEqual(1, len(result.output), 'Incorrect number of unique command outputs')
        self.assertEqual(command, result.output[0].command, 'Incorrect ResultData.command')
        self.assertEqual(0, result.output[0].returncode, 'Incorrect ResultData.returncode')
        self.assertEqual(host, result.output[0].hosts, 'Incorrect ResultData.hosts')
        self.assertEqual(['stdout'], result.output[0].stdout, 'Incorrect ResultData.stdout')
        self.assertEqual(['stderr'], result.output[0].stderr, 'Incorrect ResultData.stderr')
        self.assertEqual(False, result.output[0].timeout, 'Incorrect ResultData.timeout')
        self.assertEqual(True, result.output[0].homogeneous, 'Incorrect ResultData.homogeneous')
        self.assertEqual(True, result.output[0].passed, 'Incorrect ResultData.passed')
        self.assertEqual(host, result.output[0].passed_hosts, 'Incorrect ResultData.passed_hosts')
        self.assertEqual(
            NodeSet(), result.output[0].failed_hosts, 'Incorrect ResultData.failed_hosts')
        self.assertEqual(
            {str(host): 'stdout'}, result.output[0].all_stdout, 'Incorrect ResultData.all_stdout')
        self.assertEqual(
            {str(host): 'stderr'}, result.output[0].all_stderr, 'Incorrect ResultData.all_stderr')
