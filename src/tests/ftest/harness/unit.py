"""
  (C) Copyright 2023-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from apricot import TestWithoutServers
from ClusterShell.NodeSet import NodeSet
from data_utils import dict_extract_values, dict_subtract, list_flatten, list_stats, list_unique
from host_utils import get_local_host
from run_utils import ResultData, run_local, run_remote


class HarnessUnitTest(TestWithoutServers):
    """Harness unit tests for utilities.

    :avocado: recursive
    """

    def _verify_command_result(self, result, passed, expected, timeout, homogeneous, passed_hosts,
                               failed_hosts, all_stdout, all_stderr):
        """Verify a CommandResult object.

        Args:
            result (CommandResult): object to verify
            passed (bool): expected passed command state
            expected (list): expected list of ResultData objects
            timeout (bool): expected command timeout state
            homogeneous (bool): expected homogeneous command output state
            passed_hosts (NodeSet): expected set of hosts on which the command passed
            failed_hosts (NodeSet): expected set of hosts on which the command failed
            all_stdout (dict): expected stdout str per host key
            all_stderr (dict): expected stderr str per host key
        """
        join_stdout = '\n'.join(filter(None, all_stdout.values()))
        join_stderr = '\n'.join(filter(None, all_stderr.values()))
        self.assertEqual(passed, result.passed, 'Incorrect CommandResult.passed')
        self.assertEqual(len(expected), len(result.output), 'Incorrect CommandResult.output count')
        sorted_output = sorted(result.output)
        for index, expect in enumerate(sorted(expected)):
            actual = sorted_output[index]
            for key in ('command', 'returncode', 'hosts', 'stdout', 'stderr', 'timeout'):
                self.assertEqual(
                    getattr(expect, key), getattr(actual, key), f'Incorrect ResultData.{key}')
        self.assertEqual(timeout, result.timeout, 'Incorrect CommandResult.timeout')
        self.assertEqual(homogeneous, result.homogeneous, 'Incorrect CommandResult.homogeneous')
        self.assertEqual(passed_hosts, result.passed_hosts, 'Incorrect CommandResult.passed_hosts')
        self.assertEqual(failed_hosts, result.failed_hosts, 'Incorrect CommandResult.failed_hosts')
        self.assertEqual(all_stdout, result.all_stdout, 'Incorrect CommandResult.all_stdout')
        self.assertEqual(all_stderr, result.all_stderr, 'Incorrect CommandResult.all_stderr')
        self.assertEqual(join_stdout, result.joined_stdout, 'Incorrect CommandResult.joined_stdout')
        self.assertEqual(join_stderr, result.joined_stderr, 'Incorrect CommandResult.joined_stderr')

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

    def test_harness_unit_run_local(self):
        """Verify run_local().

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_local
        """
        host = get_local_host()
        command = 'uname -o'
        self.log_step('Verify run_local()')
        self._verify_command_result(
            result=run_local(self.log, command),
            passed=True,
            expected=[ResultData(command, 0, host, ['GNU/Linux'], [], False)],
            timeout=False,
            homogeneous=True,
            passed_hosts=host,
            failed_hosts=NodeSet(),
            all_stdout={str(host): 'GNU/Linux'},
            all_stderr={str(host): ''}
        )
        self.log_step('Unit Test Passed')

    def test_harness_unit_run_local_separated(self):
        """Verify run_local() with separate stdout and stderr.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_local_separated
        """
        host = get_local_host()
        command = 'echo stdout; echo stderr 1>&2'
        self.log_step('Verify run_local() w/ no stdout')
        self._verify_command_result(
            result=run_local(self.log, command, stderr=True),
            passed=True,
            expected=[ResultData(command, 0, host, ['stdout'], ['stderr'], False)],
            timeout=False,
            homogeneous=True,
            passed_hosts=host,
            failed_hosts=NodeSet(),
            all_stdout={str(host): 'stdout'},
            all_stderr={str(host): 'stderr'}
        )
        self.log_step('Unit Test Passed')

    def test_harness_unit_run_local_no_stdout(self):
        """Verify run_local() with no stdout.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_local_no_stdout
        """
        host = get_local_host()
        command = 'echo stderr 1>&2'
        self.log_step('Verify run_local() w/ no stdout')
        self._verify_command_result(
            result=run_local(self.log, command, stderr=True),
            passed=True,
            expected=[ResultData(command, 0, host, [], ['stderr'], False)],
            timeout=False,
            homogeneous=True,
            passed_hosts=host,
            failed_hosts=NodeSet(),
            all_stdout={str(host): ''},
            all_stderr={str(host): 'stderr'}
        )
        self.log_step('Unit Test Passed')

    def test_harness_unit_run_local_failure(self):
        """Verify run_local() with a failure.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_local_failure
        """
        host = get_local_host()
        command = 'echo fail; exit 1'
        self.log_step('Verify run_local() w/ a failure')
        self._verify_command_result(
            result=run_local(self.log, command),
            passed=False,
            expected=[ResultData(command, 1, host, ['fail'], [], False)],
            timeout=False,
            homogeneous=True,
            passed_hosts=NodeSet(),
            failed_hosts=host,
            all_stdout={str(host): 'fail'},
            all_stderr={str(host): ''}
        )
        self.log_step('Unit Test Passed')

    def test_harness_unit_run_local_timeout(self):
        """Verify run_local() with a timeout.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_local_timeout
        """
        host = get_local_host()
        command = 'echo wait; sleep 5'
        self.log_step('Verify run_local() w/ a timeout')
        self._verify_command_result(
            result=run_local(self.log, command, True, 2),
            passed=False,
            expected=[ResultData(command, 124, host, ['wait'], [], True)],
            timeout=True,
            homogeneous=True,
            passed_hosts=NodeSet(),
            failed_hosts=host,
            all_stdout={str(host): 'wait'},
            all_stderr={str(host): ''}
        )
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
        self._verify_command_result(
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
        self._verify_command_result(
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
        self._verify_command_result(
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
        command = f'echo stdout; if [ $(hostname -s) == \'{hosts[1]}\' ]; then echo stderr 1>&2; fi'
        self.log_step('Verify run_remote() w/ separated stdout and stderr')
        self._verify_command_result(
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
        command = f'echo stdout; if [ $(hostname -s) == \'{hosts[1]}\' ]; then echo stderr 1>&2; fi'
        self.log_step('Verify run_remote() w/ separated stdout and stderr')
        self._verify_command_result(
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
        """Verify run_remote() with no stdout.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_remote_no_stdout
        """
        hosts = self.get_hosts_from_yaml('test_clients', 'partition', 'reservation', '/run/hosts/*')
        command = f'if [ $(hostname -s) == \'{hosts[1]}\' ]; then echo stderr 1>&2; fi'
        self.log_step('Verify run_remote() w/ no stdout')
        self._verify_command_result(
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
        """Verify run_remote() with a failure.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_remote_failure
        """
        hosts = self.get_hosts_from_yaml('test_clients', 'partition', 'reservation', '/run/hosts/*')
        command = f'if [ $(hostname -s) == \'{hosts[1]}\' ]; then echo fail; exit 1; fi; echo pass'
        self.log_step('Verify run_remote() w/ a failure')
        self._verify_command_result(
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

    def test_harness_unit_run_remote_timeout(self):
        """Verify run_remote() with a timeout.

        :avocado: tags=all
        :avocado: tags=vm
        :avocado: tags=harness,run_utils
        :avocado: tags=HarnessUnitTest,test_harness_unit_run_remote_timeout
        """
        hosts = self.get_hosts_from_yaml('test_clients', 'partition', 'reservation', '/run/hosts/*')
        command = f'if [ $(hostname -s) == \'{hosts[1]}\' ]; then echo wait; sleep 5; fi; echo pass'
        self.log_step('Verify run_remote() w/ a timeout')
        self._verify_command_result(
            result=run_remote(self.log, hosts, command, stderr=True, timeout=2),
            passed=False,
            expected=[
                ResultData(command, 0, NodeSet(hosts[0]), ['pass'], [], False),
                ResultData(command, 124, NodeSet(hosts[1]), ['wait'], [], True),
            ],
            timeout=True,
            homogeneous=False,
            passed_hosts=NodeSet(hosts[0]),
            failed_hosts=NodeSet(hosts[1]),
            all_stdout={
                hosts[0]: 'pass',
                hosts[1]: 'wait'
            },
            all_stderr={
                hosts[0]: '',
                hosts[1]: ''
            }
        )
        self.log_step('Unit Test Passed')
