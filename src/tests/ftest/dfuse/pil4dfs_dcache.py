"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import re

from ClusterShell.NodeSet import NodeSet
from dfuse_test_base import DfuseTestBase
from dfuse_utils import Pil4dfsDcacheCmd


class Pil4dfsDcache(DfuseTestBase):
    """Test class Description: Runs set of unit test on pil4dfs directory cache.

    :avocado: recursive
    """

    _tests_suite = {
        "test_pil4dfs_dcache_enabled": [
            {
                "test_name": "test_mkdirat",
                "test_id": 0,
                "dcache_add": 2,
                "dcache_del": 0,
                "dcache_hit": 6,
                "dcache_miss": 2,
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
                "dcache_add": 3,
                "dcache_del": 2,
                "dcache_hit": 20,
                "dcache_miss": 3,
                "no_dcache_new": 0,
                "no_dcache_del": 0,
                "op_name": "rename",
                "op_count": 2
            },
            {
                "test_name": "test_open_close",
                "test_id": 4,
                "dcache_add": 1,
                "dcache_del": 0,
                "dcache_hit": 2,
                "dcache_miss": 1,
                "no_dcache_new": 0,
                "no_dcache_del": 0,
                "op_name": None
            },
            {
                "test_name": "test_dup",
                "test_id": 5,
                "dcache_add": 1,
                "dcache_del": 0,
                "dcache_hit": 2,
                "dcache_miss": 1,
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
                "no_dcache_new": 6,
                "no_dcache_del": 6,
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

    _dcache_re = {
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

    def _mount_dfuse(self):
        """Mount a DFuse mount point."""
        self.log.info("Creating DAOS pool")
        self.add_pool()

        self.log.info("Creating DAOS container")
        self.add_container(self.pool)

        self.log.info("Mounting DFuse mount point")
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

    def _update_cmd_env(self, env, mnt, timeout=None):
        """Update the Pil4dfsDcacheCmd command environment.

        Args:
            env (dict): dictionary of the command environment variables
            mnt (str): path of the Dfuse mount point
            timeout (int, optional): dir-cache timeout in seconds.
                Default is None.
        """
        lib_dir = os.path.join(self.prefix, 'lib64', 'libpil4dfs.so')
        env['LD_PRELOAD'] = lib_dir
        env['D_DFUSE_MNT'] = mnt
        env['D_LOG_MASK'] = 'DEBUG'
        env['DD_SUBSYS'] = 'il'
        env['DD_MASK'] = 'trace'
        env['D_IL_REPORT'] = '1'

        if timeout is not None:
            env["D_IL_DCACHE_REC_TIMEOUT"] = timeout

    def _check_result(self, test_case, lines):
        """Check consistency of the test output.

        Args:
            test_case (dict): dictionary of the expected output result
            lines (list): list of the test command stdout lines
        """
        dcache_count = {key: 0 for key in Pil4dfsDcache._dcache_re}
        op_re = None
        if test_case['op_name'] is not None:
            op_re = re.compile(r'^\[' + test_case['op_name'] + r'\s*\]\s+(\d+)$')
        state = 'init'
        for line in lines:
            if state == 'init':
                if Pil4dfsDcache.__start_test_re__.match(line):
                    state = 'check_dcache'

            elif state == 'check_dcache':
                for dcache_name, dcache_re in Pil4dfsDcache._dcache_re.items():
                    if dcache_re.match(line):
                        dcache_count[dcache_name] += 1
                if Pil4dfsDcache.__end_test_re__.match(line):
                    for key in Pil4dfsDcache._dcache_re:
                        self.assertEqual(
                            test_case[key],
                            dcache_count[key],
                            f"Unexpected value of dir-cache counter {key}: "
                            f"want={test_case[key]}, got={dcache_count[key]}")
                    if op_re is None:
                        return
                    state = 'check_op'

            elif state == 'check_op':
                match = op_re.match(line)
                if match:
                    self.assertEqual(
                        test_case['op_count'],
                        int(match.group(1)),
                        f"Unexpected number of operation {test_case['op_name']}: "
                        f"want={test_case['op_count']}, got={match.group(1)}")
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
        self.log_step("Mount a DFuse mount point")
        self._mount_dfuse()

        self.log.info("Running pil4dfs_dcache command")
        hostname = self.hostlist_clients[0]
        host = NodeSet(hostname)
        mnt = self.dfuse.mount_dir.value
        cmd = Pil4dfsDcacheCmd(host, self.prefix)
        self._update_cmd_env(cmd.env, mnt)

        for test_case in Pil4dfsDcache._tests_suite["test_pil4dfs_dcache_enabled"]:
            test_name = test_case['test_name']
            self.log_step(f"Run command: dcache=on, test_name={test_name}")
            cmd.update_params(test_id=test_case["test_id"])
            result = cmd.run(raise_exception=True)

            self.log_step(f"Check output command: dcache=on, test_name={test_name}")
            self._check_result(test_case, result.all_stdout[hostname].split('\n'))

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
        self.log_step("Mount a DFuse mount point")
        self._mount_dfuse()

        self.log.info("Running pil4dfs_dcache command")
        hostname = self.hostlist_clients[0]
        host = NodeSet(hostname)
        mnt = self.dfuse.mount_dir.value
        cmd = Pil4dfsDcacheCmd(host, self.prefix)
        self._update_cmd_env(cmd.env, mnt, 0)

        for test_case in Pil4dfsDcache._tests_suite["test_pil4dfs_dcache_disabled"]:
            test_name = test_case['test_name']
            self.log_step(f"Run command: dcache=off, test_name={test_name}")
            cmd.update_params(test_id=test_case["test_id"])
            result = cmd.run(raise_exception=True)

            self.log_step(f"Check output command: dcache=off, test_name={test_name}")
            self._check_result(test_case, result.all_stdout[hostname].split('\n'))

        self.log_step("Test passed")
