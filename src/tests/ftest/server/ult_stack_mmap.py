"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re

from apricot import TestWithoutServers
from ClusterShell.NodeSet import NodeSet
from command_utils import ExecutableCommand
from command_utils_base import BasicParameter
from exception_utils import CommandFailure
from host_utils import get_host_parameters, get_node_set
from run_utils import run_remote


class UltStackMmapCmd(ExecutableCommand):
    """Defines an object representing an ult_stack_mmap unit test command."""

    def __init__(self, host, path):
        """Create a UltStackMmap object.

        Args:
            host (NodeSet): host on which to remotely run the command
            path (str): path of the DAOS install directory
        """
        if len(host) != 1:
            raise ValueError(f"Invalid nodeset '{host}': waiting one client host.")

        test_dir = os.path.join(path, "lib", "daos", "TESTING", "tests")
        super().__init__("/run/ult_stack_mmap/*", "ult_stack_mmap", test_dir)

        self._host = host
        self.test_id = BasicParameter(None)

    @property
    def host(self):
        """Get the host on which to remotely run the command via run().

        Returns:
            NodeSet: remote host on which the command will run

        """
        return self._host

    def _run_process(self, raise_exception=None):
        """Run the command remotely as a foreground process.

        Args:
            raise_exception (bool, optional): whether or not to raise an exception if the command
                fails. This overrides the self.exit_status_exception setting if defined.
                Defaults to None.

        Raises:
            CommandFailure: if there is an error running the command

        Returns:
            RemoteCommandResult: a grouping of the command results from the same host with the
                same return status

        """
        if raise_exception is None:
            raise_exception = self.exit_status_exception

        # Run ult_stack_mmap remotely
        result = run_remote(self.log, self._host, self.with_exports, timeout=None)
        if raise_exception and not result.passed:
            raise CommandFailure(f"Error running UltStackMmap on host: {result.failed_hosts}\n")
        return result


class UltStackMmap(TestWithoutServers):
    """Test class Description: Runs set of unit test on ULT mmap()'ed stack.

    :avocado: recursive
    """

    _tests_suite = [
        {
            "test_name": "test_named_thread_on_xstream",
            "test_id": 0,
            "init": [],
            "run": [
                (
                    re.compile(r'^.+ DBUG .+ Created new named ULT .+ \(stack size=16384\)$'),
                    1
                ),
                (
                    re.compile(r"^Hello from mmap ULT argobot: test_named_thread_on_xstream$"),
                    1
                )
            ],
            "fini": []
        },
        {
            "test_name": "test_unnamed_thread_on_xstream",
            "test_id": 1,
            "init": [],
            "run": [
                (
                    re.compile(r'^.+ DBUG .+ Created new unnamed ULT .+ \(stack size=16384\)$'),
                    1
                ),
                (
                    re.compile(r'^.+ DBUG .+ ult_unnamed_wrapper\(\) New unnamed ULT .+$'),
                    1
                ),
                (
                    re.compile(r"^Hello from mmap ULT argobot: test_unnamed_thread_on_xstream$"),
                    1
                )
            ],
            "fini": []
        },
        {
            "test_name": "test_named_thread_on_pool",
            "test_id": 2,
            "init": [],
            "run": [
                (
                    re.compile(r'^.+ DBUG .+ Created new named ULT .+ \(stack size=16384\)$'),
                    1
                ),
                (
                    re.compile(r"^Hello from mmap ULT argobot: test_named_thread_on_pool$"),
                    1
                )
            ],
            "fini": []
        },
        {
            "test_name": "test_unnamed_thread_on_pool",
            "test_id": 3,
            "init": [],
            "run": [
                (
                    re.compile(r'^.+ DBUG .+ Created new unnamed ULT .+ \(stack size=16384\)$'),
                    1
                ),
                (
                    re.compile(r'^.+ DBUG .+ ult_unnamed_wrapper\(\) New unnamed ULT .+$'),
                    1
                ),
                (
                    re.compile(r"^Hello from mmap ULT argobot: test_unnamed_thread_on_pool$"),
                    1
                )
            ],
            "fini": []
        },
        {
            "test_name": "test_stack_size",
            "test_id": 4,
            "init": [],
            "run": [
                (
                    re.compile(r'^.+ DBUG .+ Created new unnamed ULT .+ \(stack size=65536\)$'),
                    1
                )
            ],
            "fini": []
        },
        {
            "test_name": "test_gc_001",
            "test_id": 5,
            "init": [],
            "run": [
                (
                    re.compile(r'^.+ DBUG .+ Add record .+ \(stack size 16384\) to GC list$'),
                    1
                ),
                (
                    re.compile(r'^.+ DBUG .+ Remove stack .+ \(stack size 16384\)$'),
                    64,
                ),
                (
                    re.compile(
                        r'^.+ DBUG .+ Move record .+ \(stack size 16384\) '
                        r'at the end of the GC list$'),
                    1
                )
            ],
            "fini": [
                (
                    re.compile(r'^.+ WARN .+ Memory leak detected: 3072 ULT mmap stacks not free$'),
                    1
                )
            ]
        },
        {
            "test_name": "test_gc_002",
            "test_id": 6,
            "init": [],
            "run": [
                (
                    re.compile(r'^.+ DBUG .+ Add record .+ \(stack size 65536\) to GC list$'),
                    1
                ),
                (
                    re.compile(r'^.+ DBUG .+ Remove stack .+ \(stack size 65536\)$'),
                    32,
                ),
                (
                    re.compile(r'^.+ DBUG .+ Remove record .+ \(stack size 65536\) from GC list$'),
                    1
                )
            ],
            "fini": []
        }
    ]

    __start_test_re__ = re.compile(r'^-- START of test --$')
    __end_test_re__ = re.compile(r'^-- END of test --$')

    def _get_cmd(self, hostname, test_id):
        """Return an UltStackMmapCmd command environment. """

        host = NodeSet(hostname)
        cmd = UltStackMmapCmd(host, self.prefix)

        cmd.env['D_LOG_MASK'] = 'DEBUG'
        cmd.env['DD_SUBSYS'] = 'stack'
        cmd.env['DD_MASK'] = 'mem'
        cmd.env['DAOS_ULT_STACK_MMAP'] = '1'

        cmd.update_params(test_id=test_id)

        return cmd

    def _check_test_case(self, test_case, state, res):
        """Helper checking a given state of a test case.

        Args:
            test_case (dict): Dictionary of object allowing to check the stdout of a unit test.
            state (str): State of the unit test to check
            res (list): List of regex match count
        """
        for idx, count in enumerate(res):
            self.assertEqual(
                test_case[state][idx][1],
                count,
                f"Unexpected value for regex {test_case[state][idx][0]} in state '{state}': "
                f"want={test_case[state][idx][1]}, got={count}")

    def _check_cmd_stdout(self, test_case, lines):
        """Helper checking the stdout output of a given test case.

        Args:
            test_case (dict): Dictionary of object allowing to check the stdout of a unit test.
            lines (list): Stdout lines printed by a unit test
        """
        res = [0] * len(test_case[r'init'])
        state = r'init'
        for line in lines:
            if state == r'init' and UltStackMmap.__start_test_re__.match(line):
                self._check_test_case(test_case, state, res)
                res = [0] * len(test_case[r'run'])
                state = r'run'
                continue

            if state == r'run' and UltStackMmap.__end_test_re__.match(line):
                self._check_test_case(test_case, state, res)
                res = [0] * len(test_case[r'fini'])
                state = 'fini'
                continue

            for idx, _ in enumerate(res):
                if test_case[state][idx][0].match(line):
                    res[idx] += 1

        self.assertEqual(state, r'fini', f"Unexpected state: want='fini', got ='{state}'")
        self._check_test_case(test_case, state, res)

    def test_ult_stack_nmmap(self):
        """JIRA ID: DAOS-16073.
        Test Description:
            Run unit tests of mmap()'ed ULT
            Check the output of the command

        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=server
        :avocado: tags=UltStackMmap,test_ult_stack_nmmap
        """
        hosts, _, _ = get_host_parameters(
            self, "test_clients", "client_partition", "client_reservation", "/run/hosts/*")
        hostname = get_node_set(hosts)[0]

        for test_case in UltStackMmap._tests_suite:
            self.log_step(f"Run test {test_case['test_name']}")
            cmd = self._get_cmd(hostname, test_id=test_case['test_id'])
            result = cmd.run(raise_exception=True)

            self.log_step(f"Check command output of the test {test_case['test_name']}")
            self._check_cmd_stdout(test_case, result.all_stdout[hostname].split('\n'))

        self.log_step("Test passed")
