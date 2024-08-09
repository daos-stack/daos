"""
  (C) Copyright 2021-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from apricot import TestWithServers
from command_utils_base import EnvironmentVariables
from dfuse_utils import get_dfuse, start_dfuse
from host_utils import get_local_host
from run_utils import run_remote


class ILWhiteList(TestWithServers):
    """Run whitelist_test and check the number of daos_init().

    :avocado: recursive
    """

    def run_test(self, il_lib='libpil4dfs.so', interception=False):
        """Jira ID: DAOS-15583.

        Test Description:
            Mount a DFuse mount point
            Run whitelist_test.
            Check the number of daos_init() with interception library
        """
        env = EnvironmentVariables()
        if il_lib is not None:
            env["LD_PRELOAD"] = os.path.join(self.prefix, "lib64", il_lib)

        exe_path = os.path.join(self.prefix, "lib", 'daos/TESTING/tests/whitelist_test')

        pool = self.get_pool(connect=False)
        container = self.get_container(pool)

        self.log_step('Starting dfuse')
        dfuse_hosts = get_local_host()
        dfuse = get_dfuse(self, dfuse_hosts)
        start_dfuse(self, dfuse, pool, container)

        env["D_DFUSE_MNT"] = dfuse.mount_dir.value
        env["D_LOG_MASK"] = "DEBUG"
        env["DD_SUBSYS"] = "il"
        env["DD_MASK"] = "DEBUG"

        env["D_IL_BYPASS_ALL_LIST"] = "whitelist_test"
        if interception:
            env["D_IL_INTERCEPTION_ON"] = "1"

        result = run_remote(self.log, dfuse_hosts, env.to_export_str() + exe_path)
        output = "\n".join(result.all_stdout.values())
        lines = output.split('\n')
        num_daos_init = 0
        for line in lines:
            if "called daos_init()" in line:
                num_daos_init += 1
        self.log.info('num_daos_init = %d', num_daos_init)
        return num_daos_init

    def test_whitelist_pil4dfs(self):
        """Jira ID: DAOS-15583.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=pil4dfs,dfuse
        :avocado: tags=ILWhiteList,test_whitelist_pil4dfs
        """
        num_daos_init = self.run_test(il_lib='libpil4dfs.so')
        if num_daos_init != 0:
            self.fail(f"Test failed: num_daos_init = {num_daos_init}. Expected 0.")

    def test_whitelist_pil4dfs_interception_on(self):
        """Jira ID: DAOS-15583.

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=pil4dfs,dfuse
        :avocado: tags=ILWhiteList,test_whitelist_pil4dfs_interception_on
        """
        # force function interception on which disables whitelist mode
        num_daos_init = self.run_test(il_lib='libpil4dfs.so', interception=True)
        if num_daos_init != 10:
            self.fail(f"Test failed: num_daos_init = {num_daos_init}. Expected 10.")
