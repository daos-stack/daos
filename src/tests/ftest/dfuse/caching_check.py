"""
  (C) Copyright 2019-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from general_utils import percent_change
from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics


class DfuseCachingCheck(IorTestBase):
    """Test class Description: Check dfuse read performance with and without caching
       on a single server and single client setting with basic parameters.

    :avocado: recursive
    """

    def test_dfuse_caching_check(self):
        """Jira ID: DAOS-4874.

        Test Description:
            Verify dfuse caching is working.
        Use case:
            Write using ior over dfuse with caching disabled.
            Perform ior read twice to get base read performance.
            Unmount dfuse and mount it again with caching enabled.
            Perform ior read after fresh mount to get read performance.
            Run ior again to get second read performance numbers with caching enabled.
            Compared cached read performance numbers after refresh to the baseline
            read performance and confirm cached read performance is multiple folds
            higher than with caching disabled.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=daosio,dfuse
        :avocado: tags=DfuseCachingCheck,test_dfuse_caching_check
        """
        # get params
        flags = self.params.get("iorflags", '/run/ior/*')
        read_x = self.params.get("read_x", "/run/ior/*", 1)

        # update flag
        self.ior_cmd.update_params(flags=flags[0])

        self.log_step('Write to the dfuse mount point')
        self.run_ior_with_pool(fail_on_warning=False, stop_dfuse=False)
        print(self.dfuse.get_stats())

        self.log_step('Get baseline read performance from dfuse with caching disabled')
        self.ior_cmd.update_params(flags=flags[1])
        base_read_arr = []
        out = self.run_ior_with_pool(fail_on_warning=False, stop_dfuse=False)
        base_read_arr.append(IorCommand.get_ior_metrics(out))
        print(self.dfuse.get_stats())

        out = self.run_ior_with_pool(fail_on_warning=False, stop_dfuse=False)
        base_read_arr.append(IorCommand.get_ior_metrics(out))
        print(self.dfuse.get_stats())

        # the index of max_mib
        max_mib = IorMetrics.MAX_MIB

        self.log_step('Re-mount dfuse with caching enabled')
        self.dfuse.unmount(tries=1)
        self.dfuse.update_params(disable_caching=False)
        self.dfuse.run()

        self.log_step('Get first read performance with caching enabled')
        out = self.run_ior_with_pool(fail_on_warning=False, stop_dfuse=False)
        base_read_arr.append(IorCommand.get_ior_metrics(out))

        print(self.dfuse.get_stats())

        self.log_step('Get cached read performance')
        out = self.run_ior_with_pool(fail_on_warning=False, stop_dfuse=False)
        with_caching = IorCommand.get_ior_metrics(out)

        print(self.dfuse.get_stats())
        self.dfuse.unmount(tries=1)

        self.log_step('Verify cached read performance is greater than first read')
        # Log all the values first, then do the assert so that failures can be checked easily.
        for base_read in base_read_arr:
            actual_change = percent_change(base_read[0][max_mib], with_caching[0][max_mib])
            self.log.info('assert actual_change > min_change: %f > %f', actual_change, read_x)
        for base_read in base_read_arr:
            actual_change = percent_change(base_read[0][max_mib], with_caching[0][max_mib])
            if actual_change < read_x:
                self.fail('Expected a speedup of {} but got {}'.format(read_x, actual_change))
