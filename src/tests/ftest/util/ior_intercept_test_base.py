"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics
from general_utils import percent_change


class IorInterceptTestBase(IorTestBase):
    """Base IOR interception test class.

    :avocado: recursive
    """

    def run_il_perf_check(self, libname):
        """Verify IOR performance with DFUSE + IL is similar to DFS.

        Steps:
            Run IOR with DFS.
            Run IOR with DFUSE + IL.
            Verify performance with DFUSE + IL is similar to DFS.

        """
        if libname is None:
            self.fail("libname is not set for function interception.")
        # Write and read performance thresholds
        write_x = self.params.get("write_x", self.ior_cmd.namespace, None)
        read_x = self.params.get("read_x", self.ior_cmd.namespace, None)
        if write_x is None or read_x is None:
            self.fail("Failed to get write_x and read_x from config")

        # Run IOR with DFS
        self.ior_cmd.api.update("DFS")
        dfs_out = self.run_ior_with_pool(fail_on_warning=self.log.info)
        dfs_perf = IorCommand.get_ior_metrics(dfs_out)

        # Destroy and use a new pool and container
        self.container.destroy()
        self.container = None
        self.pool.destroy()
        self.pool = None

        # Run IOR with dfuse + IL
        self.ior_cmd.api.update("POSIX")
        dfuse_out = self.run_ior_with_pool(
            intercept=os.path.join(self.prefix, 'lib64', libname),
            fail_on_warning=self.log.info)
        dfuse_perf = IorCommand.get_ior_metrics(dfuse_out)

        # Verify write and read performance are within the thresholds.
        # Since Min can have a lot of variance, don't check Min or Mean.
        # Ideally, we might want to look at the Std Dev to ensure the results are admissible.
        # Log some params for debugging.
        server_provider = self.server_managers[0].get_config_value("provider")
        self.log.info("Provider:           %s", server_provider)
        self.log.info("Library:            %s", libname)
        self.log.info("Servers:            %s", self.hostlist_servers)
        self.log.info("Clients:            %s", self.hostlist_clients)
        self.log.info("PPN:                %s", self.ppn)
        self.log.info("IOR stonewall:      %s", self.ior_cmd.sw_deadline.value)
        self.log.info("IOR dfs_oclass:     %s", self.ior_cmd.dfs_oclass.value)
        self.log.info("IOR transfer size:  %s", self.ior_cmd.transfer_size.value)
        self.log.info("IOR repetitions:    %s", self.ior_cmd.repetitions.value)

        dfs_max_write = float(dfs_perf[0][IorMetrics.MAX_MIB])
        dfuse_max_write = float(dfuse_perf[0][IorMetrics.MAX_MIB])
        actual_write_x = percent_change(dfs_max_write, dfuse_max_write)
        self.log.info("DFS Max Write:      %.2f", dfs_max_write)
        self.log.info("DFUSE IL Max Write: %.2f", dfuse_max_write)
        self.log.info("Percent Diff:       %.2f%%", actual_write_x * 100)

        dfs_max_read = float(dfs_perf[1][IorMetrics.MAX_MIB])
        dfuse_max_read = float(dfuse_perf[1][IorMetrics.MAX_MIB])
        actual_read_x = percent_change(dfs_max_read, dfuse_max_read)
        self.log.info("DFS Max Read:       %.2f", dfs_max_read)
        self.log.info("DFUSE IL Max Read:  %.2f", dfuse_max_read)
        self.log.info("Percent Diff:       %.2f%%", actual_read_x * 100)

        self.assertLessEqual(abs(actual_write_x), write_x, "Max Write Diff too large")
        self.assertLessEqual(abs(actual_read_x), read_x, "Max Read Diff too large")
