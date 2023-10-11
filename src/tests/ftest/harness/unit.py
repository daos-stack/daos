"""
  (C) Copyright 2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ClusterShell.NodeSet import NodeSet

from apricot import TestWithoutServers
from data_utils import list_unique, list_flatten, list_stats, \
    dict_extract_values, dict_subtract
from run_utils import run_remote, ResultData


class HarnessUnitTest(TestWithoutServers):
    """Harness unit tests for utilities.

    :avocado: recursive
    """

    def _verify_remote_command_result(self, result, passed, expected, timeout, homogeneous,
                                      passed_hosts, failed_hosts, all_stdout, all_stderr):
        """Verify a RemoteCommandResult object.

        Args:
            result (RemoteCommandResult): object to verify
            passed (bool): expected passed command state
            expected (list): expected list of ResultData objects
            timeout (bool): expected command timeout state
            homogeneous (bool): expected homogeneous command output state
            passed_hosts (NodeSet): expected set of hosts on which the command passed
            failed_hosts (NodeSet): expected set of hosts on which the command failed
            all_stdout (dict): expected stdout str per host key
            all_stderr (dict): expected stderr str per host key
        """
        self.assertEqual(passed, result.passed, 'Incorrect RemoteCommandResult.passed')
        self.assertEqual(
            len(expected), len(result.output), 'Incorrect RemoteCommandResult.output count')
        sorted_output = sorted(result.output)
        for index, expect in enumerate(sorted(expected)):
            actual = sorted_output[index]
            for key in ('command', 'returncode', 'hosts', 'stdout', 'stderr', 'timeout'):
                self.assertEqual(
                    getattr(expect, key), getattr(actual, key),
                    'Incorrect ResultData.{}'.format(key))
        self.assertEqual(timeout, result.timeout, 'Incorrect RemoteCommandResult.timeout')
        self.assertEqual(
            homogeneous, result.homogeneous, 'Incorrect RemoteCommandResult.homogeneous')
        self.assertEqual(
            passed_hosts, result.passed_hosts, 'Incorrect RemoteCommandResult.passed_hosts')
        self.assertEqual(
            failed_hosts, result.failed_hosts, 'Incorrect RemoteCommandResult.failed_hosts')
        self.assertEqual(all_stdout, result.all_stdout, 'Incorrect RemoteCommandResult.all_stdout')
        self.assertEqual(all_stderr, result.all_stderr, 'Incorrect RemoteCommandResult.all_stderr')

    def test_harness_unit_list_unique(self):
        """Verify list_unique().

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,dict_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_list_unique
        """
        self.log_step('Verify list_unique()')
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
        self.log_step('Unit Test Passed')

    def test_harness_unit_list_flatten(self):
        """Verify list_flatten().

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,dict_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_list_flatten
        """
        self.log_step('Verify list_flatten()')
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
        self.log_step('Unit Test Passed')

    def test_harness_unit_list_stats(self):
        """Verify list_stats().

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,dict_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_list_stats
        """
        self.log_step('Verify list_stats()')
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
        self.log_step('Unit Test Passed')

    def test_harness_unit_dict_extract_values(self):
        """Verify dict_extract_values().

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,dict_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_dict_extract_values
        """
        self.log_step('Verify dict_extract_values()')
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
        self.log_step('Unit Test Passed')

    def test_harness_unit_dict_subtract(self):
        """Verify dict_subtract().

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,dict_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_dict_subtract
        """
        self.log_step('Verify dict_subtract()')
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
        self.log_step('Unit Test Passed')

    def test_harness_unit_run_remote_single(self):
        """Verify run_remote() with a single host.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_remote_single
        """
        hosts = self.get_hosts_from_yaml('test_clients', 'partition', 'reservation', '/run/hosts/*')
        command = 'uname -o'
        self.log_step('Verify run_remote() w/ single host')
        self._verify_remote_command_result(
            result=run_remote(self.log, NodeSet(hosts[0]), command),
            passed=True,
            expected=[ResultData(command, 0, NodeSet(hosts[0]), ['GNU/Linux'], [], False)],
            timeout=False,
            homogeneous=True,
            passed_hosts=NodeSet(hosts[0]),
            failed_hosts=NodeSet(),
            all_stdout={hosts[0]: 'GNU/Linux'},
            all_stderr={hosts[0]: ''}
        )
        self.log_step('Unit Test Passed')

    def test_harness_unit_run_remote_homogeneous(self):
        """Verify run_remote() with homogeneous output.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_remote_homogeneous
        """
        hosts = self.get_hosts_from_yaml('test_clients', 'partition', 'reservation', '/run/hosts/*')
        command = 'uname -o'
        self.log_step('Verify run_remote() w/ homogeneous output')
        self._verify_remote_command_result(
            result=run_remote(self.log, hosts, command),
            passed=True,
            expected=[ResultData(command, 0, hosts, ['GNU/Linux'], [], False)],
            timeout=False,
            homogeneous=True,
            passed_hosts=hosts,
            failed_hosts=NodeSet(),
            all_stdout={str(hosts): 'GNU/Linux'},
            all_stderr={str(hosts): ''}
        )
        self.log_step('Unit Test Passed')

    def test_harness_unit_run_remote_heterogeneous(self):
        """Verify run_remote() with heterogeneous output.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_remote_heterogeneous
        """
        hosts = self.get_hosts_from_yaml('test_clients', 'partition', 'reservation', '/run/hosts/*')
        command = 'hostname -s'
        self.log_step('Verify run_remote() w/ heterogeneous output')
        self._verify_remote_command_result(
            result=run_remote(self.log, hosts, command),
            passed=True,
            expected=[
                ResultData(command, 0, NodeSet(hosts[0]), [hosts[0]], [], False),
                ResultData(command, 0, NodeSet(hosts[1]), [hosts[1]], [], False),
            ],
            timeout=False,
            homogeneous=False,
            passed_hosts=hosts,
            failed_hosts=NodeSet(),
            all_stdout={
                hosts[0]: hosts[0],
                hosts[1]: hosts[1]
            },
            all_stderr={
                hosts[0]: '',
                hosts[1]: ''
            },
        )
        self.log_step('Unit Test Passed')

    def test_harness_unit_run_remote_combined(self):
        """Verify run_remote() with combined stdout and stderr.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_remote_combined
        """
        hosts = self.get_hosts_from_yaml('test_clients', 'partition', 'reservation', '/run/hosts/*')
        command = 'echo stdout; if [ $(hostname -s) == \'{}\' ]; then echo stderr 1>&2; fi'.format(
            hosts[1])
        self.log_step('Verify run_remote() w/ separated stdout and stderr')
        self._verify_remote_command_result(
            result=run_remote(self.log, hosts, command, stderr=False),
            passed=True,
            expected=[
                ResultData(command, 0, NodeSet(hosts[0]), ['stdout'], [], False),
                ResultData(command, 0, NodeSet(hosts[1]), ['stdout', 'stderr'], [], False),
            ],
            timeout=False,
            homogeneous=False,
            passed_hosts=hosts,
            failed_hosts=NodeSet(),
            all_stdout={
                hosts[0]: 'stdout',
                hosts[1]: 'stdout\nstderr'
            },
            all_stderr={
                hosts[0]: '',
                hosts[1]: ''
            }
        )
        self.log_step('Unit Test Passed')

    def test_harness_unit_run_remote_separated(self):
        """Verify run_remote() with separated stdout and stderr.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_remote_separated
        """
        hosts = self.get_hosts_from_yaml('test_clients', 'partition', 'reservation', '/run/hosts/*')
        command = 'echo stdout; if [ $(hostname -s) == \'{}\' ]; then echo stderr 1>&2; fi'.format(
            hosts[1])
        self.log_step('Verify run_remote() w/ separated stdout and stderr')
        self._verify_remote_command_result(
            result=run_remote(self.log, hosts, command, stderr=True),
            passed=True,
            expected=[
                ResultData(command, 0, NodeSet(hosts[0]), ['stdout'], [], False),
                ResultData(command, 0, NodeSet(hosts[1]), ['stdout'], ['stderr'], False),
            ],
            timeout=False,
            homogeneous=False,
            passed_hosts=hosts,
            failed_hosts=NodeSet(),
            all_stdout={
                hosts[0]: 'stdout',
                hosts[1]: 'stdout'
            },
            all_stderr={
                hosts[0]: '',
                hosts[1]: 'stderr'
            }
        )
        self.log_step('Unit Test Passed')

    def test_harness_unit_run_remote_no_stdout(self):
        """Verify run_remote() with separated stdout and stderr.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_remote_no_stdout
        """
        hosts = self.get_hosts_from_yaml('test_clients', 'partition', 'reservation', '/run/hosts/*')
        command = 'if [ $(hostname -s) == \'{}\' ]; then echo stderr 1>&2; fi'.format(hosts[1])
        self.log_step('Verify run_remote() w/ no stdout')
        self._verify_remote_command_result(
            result=run_remote(self.log, hosts, command, stderr=True),
            passed=True,
            expected=[
                ResultData(command, 0, NodeSet(hosts[0]), [], [], False),
                ResultData(command, 0, NodeSet(hosts[1]), [], ['stderr'], False),
            ],
            timeout=False,
            homogeneous=False,
            passed_hosts=hosts,
            failed_hosts=NodeSet(),
            all_stdout={
                hosts[0]: '',
                hosts[1]: ''
            },
            all_stderr={
                hosts[0]: '',
                hosts[1]: 'stderr'
            }
        )
        self.log_step('Unit Test Passed')

    def test_harness_unit_run_remote_failure(self):
        """Verify run_remote() with separated stdout and stderr.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_remote_failure
        """
        hosts = self.get_hosts_from_yaml('test_clients', 'partition', 'reservation', '/run/hosts/*')
        command = 'if [ $(hostname -s) == \'{}\' ]; then echo fail; exit 1; fi; echo pass'.format(
            hosts[1])
        self.log_step('Verify run_remote() w/ a failure')
        self._verify_remote_command_result(
            result=run_remote(self.log, hosts, command, stderr=True),
            passed=False,
            expected=[
                ResultData(command, 0, NodeSet(hosts[0]), ['pass'], [], False),
                ResultData(command, 1, NodeSet(hosts[1]), ['fail'], [], False),
            ],
            timeout=False,
            homogeneous=False,
            passed_hosts=NodeSet(hosts[0]),
            failed_hosts=NodeSet(hosts[1]),
            all_stdout={
                hosts[0]: 'pass',
                hosts[1]: 'fail'
            },
            all_stderr={
                hosts[0]: '',
                hosts[1]: ''
            }
        )
        self.log_step('Unit Test Passed')
