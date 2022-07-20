#!/usr/bin/env python3
"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics
from general_utils import percent_change


class IorInterceptTestBase(IorTestBase):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-few-public-methods
    """Base IOR interception test class.

    :avocado: recursive
    """

    def run_il_perf_check(self):
        """Verify IOR performance with DFUSE + IL is similar to DFS.

        Steps:
            Run IOR with DFS.
            Run IOR with DFUSE + IL.
            Verify performance with DFUSE + IL is similar to DFS.

        """
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
            intercept=os.path.join(self.prefix, 'lib64', 'libioil.so'),
            fail_on_warning=self.log.info)
        dfuse_perf = IorCommand.get_ior_metrics(dfuse_out)

        # Verify write and read performance are within the thresholds.
        # Since Min can have a lot of variance, don't check Min or Mean.
        # Ideally, we might want to look at the Std Dev to ensure the results are admissible.
        # Log the provider for debugging.
        server_provider = self.server_managers[0].get_config_value("provider")
        self.log.info("Provider:           %s", server_provider)

        dfs_max_write = float(dfs_perf[0][IorMetrics.Max_MiB])
        dfuse_max_write = float(dfuse_perf[0][IorMetrics.Max_MiB])
        actual_write_x = percent_change(dfs_max_write, dfuse_max_write)
        self.log.info("DFS Max Write:      %.2f", dfs_max_write)
        self.log.info("DFUSE IL Max Write: %.2f", dfuse_max_write)
        self.log.info("Percent Diff:       %.2f%%", actual_write_x * 100)

        dfs_max_read = float(dfs_perf[1][IorMetrics.Max_MiB])
        dfuse_max_read = float(dfuse_perf[1][IorMetrics.Max_MiB])
        actual_read_x = percent_change(dfs_max_read, dfuse_max_read)
        self.log.info("DFS Max Read:      %.2f", dfs_max_read)
        self.log.info("DFUSE IL Max Read: %.2f", dfuse_max_read)
        self.log.info("Percent Diff:      %.2f%%", actual_read_x * 100)

        self.assertLessEqual(abs(actual_write_x), write_x, "Max Write Diff too large")
        self.assertLessEqual(abs(actual_read_x), read_x, "Max Read Diff too large")
