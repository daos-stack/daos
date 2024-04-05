"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import re

from command_utils import ExecutableCommand
from command_utils_base import BasicParameter
from dfuse_test_base import DfuseTestBase
from exception_utils import CommandFailure
from run_utils import run_remote


class Pil4dfsDcacheCmd(ExecutableCommand):
    """Defines an object representing a pil4dfs_dcache unit test command."""

    def __init__(self, host, path, mnt, timeout=None):
        """Create a Pil4dfsDcacheUtestCmd object.

        Args:
            host (str): Hostname on which to remotely run the command
            path (str): path of the DAOS install directory
            mnt (str): path of the Dfuse mount point
            timeout (int, optional): dir-cache timeout in seconds.
                Default is None.
        """
        test_dir = os.path.join(path, "lib", "daos", "TESTING", "tests")
        super().__init__("/run/pil4dfs_dcache/*", "pil4dfs_dcache", test_dir)

        self._host = host

        lib_dir = os.path.join(path, 'lib64', 'libpil4dfs.so')
        self.env['LD_PRELOAD'] = lib_dir
        self.env['D_DFUSE_MNT'] = mnt
        self.env['D_LOG_MASK'] = 'DEBUG'
        self.env['DD_SUBSYS'] = 'il'
        self.env['DD_MASK'] = 'trace'
        self.env['D_IL_REPORT'] = '1'

        if timeout is not None:
            self.env["D_IL_DCACHE_REC_TIMEOUT"] = timeout

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

        # Run pil4dfs_dcache remotely
        result = run_remote(self.log, self._host, self.with_exports, timeout=None)
        if raise_exception and not result.passed:
            stdout = result.all_stdout[self.host].replace(r'\n', os.linesep)
            stderr = result.all_stderr[self.host].replace(r'\n', os.linesep)
            raise CommandFailure(
                f"Error running pil4dfs_dcache_utest on: {result.failed_hosts}\n"
                f"-- STDERR START --\n{stderr}\n-- STDERR END --\n"
                f"-- STDOUT START --\n{stdout}\n-- STDOUT END --\n")
        return result


class Pil4dfsDcache(DfuseTestBase):
    """Test class Description: Runs set of unit test on pil4dfs directory cache.

    :avocado: recursive
    """

    __tests_suite__ = {
        "test_pil4dfs_dcache_enabled": [
            {
                "test_name": "test_mkdirat",
                "test_id": 0,
                "dcache_add": 3,
                "dcache_del": 0,
                "dcache_hit": 5,
                "dcache_miss": 3,
                "no_dcache_new": 0,
                "no_dcache_del": 0,
                "op_name": "mkdir",
                "op_count": 4
            },
            {
                "test_name": "test_unlinkat",
                "test_id": 1,
                "dcache_add": 0,
                "dcache_del": 2,
                "dcache_hit": 8,
                "dcache_miss": 0,
                "no_dcache_new": 0,
                "no_dcache_del": 0,
                "op_name": "unlink",
                "op_count": 4
            },
            {
                "test_name": "test_rmdir",
                "test_id": 2,
                "dcache_add": 0,
                "dcache_del": 2,
                "dcache_hit": 8,
                "dcache_miss": 0,
                "no_dcache_new": 0,
                "no_dcache_del": 0,
                "op_name": "rmdir",
                "op_count": 4
            },
            {
                "test_name": "test_rename",
                "test_id": 3,
                "dcache_add": 2,
                "dcache_del": 1,
                "dcache_hit": 15,
                "dcache_miss": 2,
                "no_dcache_new": 0,
                "no_dcache_del": 0,
                "op_name": "rename",
                "op_count": 2
            },
            {
                "test_name": "test_open_close",
                "test_id": 4,
                "dcache_add": 2,
                "dcache_del": 0,
                "dcache_hit": 1,
                "dcache_miss": 2,
                "no_dcache_new": 0,
                "no_dcache_del": 0,
                "op_name": None
            },
            {
                "test_name": "test_dup",
                "test_id": 5,
                "dcache_add": 2,
                "dcache_del": 0,
                "dcache_hit": 1,
                "dcache_miss": 2,
                "no_dcache_new": 0,
                "no_dcache_del": 0,
                "op_name": None
            }
        ],
        "test_pil4dfs_dcache_disabled": [
            {
                "test_name": "test_mkdirat",
                "test_id": 0,
                "dcache_add": 0,
                "dcache_del": 0,
                "dcache_hit": 0,
                "dcache_miss": 0,
                "no_dcache_new": 4,
                "no_dcache_del": 4,
                "op_name": "mkdir",
                "op_count": 4
            },
            {
                "test_name": "test_unlinkat",
                "test_id": 1,
                "dcache_add": 0,
                "dcache_del": 0,
                "dcache_hit": 0,
                "dcache_miss": 0,
                "no_dcache_new": 4,
                "no_dcache_del": 4,
                "op_name": "unlink",
                "op_count": 4
            },
            {
                "test_name": "test_rmdir",
                "test_id": 2,
                "dcache_add": 0,
                "dcache_del": 0,
                "dcache_hit": 0,
                "dcache_miss": 0,
                "no_dcache_new": 4,
                "no_dcache_del": 4,
                "op_name": "rmdir",
                "op_count": 4
            },
            {
                "test_name": "test_rename",
                "test_id": 3,
                "dcache_add": 0,
                "dcache_del": 0,
                "dcache_hit": 0,
                "dcache_miss": 0,
                "no_dcache_new": 5,
                "no_dcache_del": 5,
                "op_name": "rename",
                "op_count": 2
            },
            {
                "test_name": "test_open_close",
                "test_id": 4,
                "dcache_add": 0,
                "dcache_del": 0,
                "dcache_hit": 0,
                "dcache_miss": 0,
                "no_dcache_new": 2,
                "no_dcache_del": 2,
                "op_name": None
            },
            {
                "test_name": "test_dup",
                "test_id": 5,
                "dcache_add": 0,
                "dcache_del": 0,
                "dcache_hit": 0,
                "dcache_miss": 0,
                "no_dcache_new": 2,
                "no_dcache_del": 2,
                "op_name": None
            }
        ],
    }

    __dcache_re__ = {
        "dcache_add": re.compile(r'^.+ il +DBUG .+ dcache_add\(\) .+$'),
        "dcache_del": re.compile(r'^.+ il +DBUG .+ dcache_rec_free\(\) .+$'),
        "dcache_hit": re.compile(r'^.+ il +DBUG .+ dcache hit:.+$'),
        "dcache_miss": re.compile(r'^.+ il +DBUG .+ dcache miss:.+$'),
        "no_dcache_new": re.compile(r'^.+ il +DBUG .+ dcache_find_insert_dact\(\).+$'),
        "no_dcache_del": re.compile(r'^.+ il +DBUG .+ drec_del_at_dact\(\) .+$')
    }

    __start_test_re__ = re.compile(r'^-- START of test_.+ --$')
    __end_test_re__ = re.compile(r'^-- END of test_.+ --$')

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super().setUp()

        self.log.info("Creating DAOS pool")
        self.add_pool()
        self.log.debug("Created DAOS pool %s", str(self.pool))

        self.log.info("Creating DAOS container")
        self.add_container(self.pool)
        self.log.debug("Created DAOS container %s", str(self.container))

        self.log.info("Mounting DFuse mount point")
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)
        self.log.debug("Mounted DFuse mount point %s", self.dfuse.mount_dir.value)

    def check_result(self, test_case, lines):
        """Check consistency of the test output.

        Args:
            test_case (dict): dictionary of the expected output result
            lines (list): list of the test command stdout lines
        """
        dcache_count = {key: 0 for key in Pil4dfsDcache.__dcache_re__}
        op_re = None
        if test_case['op_name'] is not None:
            op_re = re.compile(r'^\[' + test_case['op_name'] + r'\s*\]\s+(\d+)$')
        state = 'init'
        for line in lines:
            if state == 'init':
                if Pil4dfsDcache.__start_test_re__.match(line):
                    state = 'check_dcache'

            elif state == 'check_dcache':
                for dcache_name, dcache_re in Pil4dfsDcache.__dcache_re__.items():
                    if dcache_re.match(line):
                        dcache_count[dcache_name] += 1
                if Pil4dfsDcache.__end_test_re__.match(line):
                    for key in Pil4dfsDcache.__dcache_re__:
                        self.assertEqual(test_case[key], dcache_count[key])
                    if op_re is None:
                        return
                    state = 'check_op'

            elif state == 'check_op':
                match = op_re.match(line)
                if match:
                    self.assertEqual(test_case['op_count'], int(match.group(1)))
                    return

        self.fail(f"Test failed: state={state}\n")

    def test_pil4dfs_dcache_enabled(self):
        """Jira ID: DAOS-14348.

        Test Description:
            Mount a DFuse mount point
            Run unit tests of test_pil4dfs_dcache with dir-cache enabled
            Check the output of the command

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pil4dfs,dcache,dfuse
        :avocado: tags=Pil4dfsDcache,test_pil4dfs_dcache_enabled
        """

        self.log.info("Running pil4dfs_dcache command")
        host = self.hostlist_clients[0]
        mnt = self.dfuse.mount_dir.value
        cmd = Pil4dfsDcacheCmd(host, self.prefix, mnt)

        for test_case in Pil4dfsDcache.__tests_suite__["test_pil4dfs_dcache_enabled"]:
            cmd.update_params(test_id=test_case["test_id"])
            result = cmd.run(raise_exception=True)
            self.check_result(test_case, result.all_stdout[host].split('\n'))

        self.log_step("Test passed")

    def test_pil4dfs_dcache_disabled(self):
        """Jira ID: DAOS-14348.

        Test Description:
            Mount a DFuse mount point
            Run unit tests of test_pil4dfs_dcache with dir-cache disabled
            Check the output of the command

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pil4dfs,dcache,dfuse
        :avocado: tags=Pil4dfsDcache,test_pil4dfs_dcache_disabled
        """

        self.log.info("Running pil4dfs_dcache command")
        host = self.hostlist_clients[0]
        mnt = self.dfuse.mount_dir.value
        cmd = Pil4dfsDcacheCmd(host, self.prefix, mnt, 0)

        for test_case in Pil4dfsDcache.__tests_suite__["test_pil4dfs_dcache_disabled"]:
            cmd.update_params(test_id=test_case["test_id"])
            result = cmd.run(raise_exception=True)
            self.check_result(test_case, result.all_stdout[host].split('\n'))

        self.log_step("Test passed")
