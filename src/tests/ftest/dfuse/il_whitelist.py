"""
  (C) Copyright 2021-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from apricot import TestWithServers
from ClusterShell.NodeSet import NodeSet
from dfuse_utils import WhiteListCmd, get_dfuse, start_dfuse


class ILWhiteList(TestWithServers):
    """Run whitelist_test and check the number of daos_init().

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # obtain separate logs
        self.update_log_file_names()

        # Start the servers and agents
        super().setUp()

    def _mount_dfuse(self):
        """Mount a DFuse mount point.

        Returns:
            Dfuse: a Dfuse object
        """
        self.log.info("Creating DAOS pool")
        pool = self.get_pool()

        self.log.info("Creating DAOS container")
        container = self.get_container(pool)

        self.log.info("Mounting DFuse mount point")
        dfuse = get_dfuse(self, self.hostlist_clients)
        start_dfuse(self, dfuse, pool, container)
        return dfuse

    def run_test(self, il_lib='libpil4dfs.so', bypass_all=False, bypass=False):
        """Jira ID: DAOS-15583.

        Test Description:
            Mount a DFuse mount point
            Run whitelist_test.
            Check the number of daos_init() with interception library
        """
        dfuse = self._mount_dfuse()
        hostname = self.hostlist_clients[0]
        host = NodeSet(hostname)
        cmd = WhiteListCmd(host, self.prefix)

        cmd.env['D_DFUSE_MNT'] = dfuse.mount_dir.value
        cmd.env['D_LOG_MASK'] = 'DEBUG'
        cmd.env['DD_SUBSYS'] = 'il'
        cmd.env['DD_MASK'] = 'DEBUG'

        if il_lib is not None:
            lib_dir = os.path.join(self.prefix, 'lib64', il_lib)
            cmd.env['LD_PRELOAD'] = lib_dir
        if bypass_all:
            cmd.env['D_IL_BYPASS_ALL_LIST'] = 'whitelist_test'
        if bypass:
            cmd.env['D_IL_BYPASS_LIST'] = 'whitelist_test'

        result = cmd.run(raise_exception=True)
        lines = result.all_stdout[hostname].split('\n')
        num_daos_init = 0
        for line in lines:
            if "called daos_init()" in line:
                num_daos_init += 1
        print(f'num_daos_init = {num_daos_init}')
        return num_daos_init

    def test_whitelist_pil4dfs(self):
        """Jira ID: DAOS-15583.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=pil4dfs,dfuse
        :avocado: tags=ILWhiteList,test_whitelist_pil4dfs
        """
        num_daos_init = self.run_test(il_lib='libpil4dfs.so')
        if num_daos_init != 10:
            self.fail(f"Test failed: num_daos_init = {num_daos_init}. Expected 10.\n")

    def test_whitelist_pil4dfs_bypass(self):
        """Jira ID: DAOS-15583.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=pil4dfs,dfuse
        :avocado: tags=ILWhiteList,test_whitelist_pil4dfs_bypass
        """
        num_daos_init = self.run_test(il_lib='libpil4dfs.so', bypass=True)
        if num_daos_init != 5:
            self.fail(f"Test failed: num_daos_init = {num_daos_init}. Expected 5.\n")

    def test_whitelist_pil4dfs_bypassall(self):
        """Jira ID: DAOS-15583.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=pil4dfs,dfuse
        :avocado: tags=ILWhiteList,test_whitelist_pil4dfs_bypassall
        """
        num_daos_init = self.run_test(il_lib='libpil4dfs.so', bypass_all=True)
        if num_daos_init != 0:
            self.fail(f"Test failed: num_daos_init = {num_daos_init}. Expected 0.\n")
