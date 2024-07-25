"""
  (C) Copyright 2021-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from host_utils import get_local_host
from run_utils import run_remote


class ILWhiteList(TestWithServers):
    """Run whitelist_test and check the number of daos_init().

    :avocado: recursive
    """

    def run_test(self, il_lib='libpil4dfs.so', bypass_all=False, bypass=False):
        """Jira ID: DAOS-15583.

        Test Description:
            Mount a DFuse mount point
            Run whitelist_test.
            Check the number of daos_init() with interception library
        """
        if il_lib is not None:
            lib_path = os.path.join(self.prefix, "lib64", il_lib)
            env_str = f"export LD_PRELOAD={lib_path}; "
        else:
            env_str = ""
        exe_path = os.path.join(self.prefix, "lib", 'daos/TESTING/tests/whitelist_test')

        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        self.log_step('Starting dfuse')
        dfuse_hosts = get_local_host()
        dfuse = get_dfuse(self, dfuse_hosts)
        start_dfuse(self, dfuse, pool, container)
        fuse_root_dir = dfuse.mount_dir.value

        env_str += f'export D_DFUSE_MNT={fuse_root_dir}; '
        env_str += 'export D_LOG_MASK=DEBUG; export DD_SUBSYS=il; export DD_MASK=DEBUG; '

        if bypass_all:
            env_str += 'export D_IL_BYPASS_ALL_LIST=whitelist_test; '
        if bypass:
            env_str += 'export D_IL_BYPASS_LIST=whitelist_test; '

        result = run_remote(self.log, dfuse_hosts, env_str + exe_path)
        hostname = self.hostlist_servers[0]
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
