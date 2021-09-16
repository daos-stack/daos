#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics
from general_utils import pcmd

class DfuseCachingCheck(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Check dfuse read performance with and
       without caching on a single server and single client setting with
       basic parameters.

    :avocado: recursive
    """

    def test_dfuse_caching_check(self):
        """Jira ID: DAOS-4874.

        Test Description:
            Purpose of this test is to check if dfuse caching is working.
        Use case:
            Write using ior over dfuse with caching disabled.
            Perform ior read to get base read performance.
            Run ior read to get second read performance with caching disabled.
            Compare first and second read performance numbers and they should
            be similar.
            Unmount dfuse and mount it again with caching enabled.
            Perform ior read after fresh mount to get read performance.
            Run ior again to get second read performance numbers with caching
            enabled.
            Compare first and second read performance numbers after dfuse
            refresh and the second read should be multiple folds higher than
            the first one.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=daosio,dfuse
        :avocado: tags=dfusecachingcheck
        """
        # get params
        flags = self.params.get("iorflags", '/run/ior/*')
        read_x = self.params.get("read_x", "/run/ior/*", 1)

        # update flag
        self.ior_cmd.flags.update(flags[0])

        # run ior to write to the dfuse mount point
        self.run_ior_with_pool(fail_on_warning=False, stop_dfuse=False)

        # update ior flag to read
        self.ior_cmd.flags.update(flags[1])
        # run ior to read and store the read performance
        out = self.run_ior_with_pool(fail_on_warning=False, stop_dfuse=False)
        base_read = IorCommand.get_ior_metrics(out)
        # run ior again to read with caching disabled and store performance
        out = self.run_ior_with_pool(fail_on_warning=False, stop_dfuse=False)
        without_caching = IorCommand.get_ior_metrics(out)
        max_mib = int(IorMetrics.Max_MiB)
        # Compare read performance with caching disabled
        # it should be similar to last read
        lower_bound = (float(base_read[0][max_mib]) -
                       (float(base_read[0][max_mib]) * read_x[0]))
        upper_bound = (float(base_read[0][max_mib]) +
                       (float(base_read[0][max_mib]) * read_x[0]))
        # verify read performance is similar to last read and within
        # the range of 1% up or down the first read performance
        self.assertTrue(lower_bound <= float(without_caching[0][max_mib])
                        <= upper_bound)

        # unmount dfuse and mount again with caching enabled
        pcmd(self.hostlist_clients,
             self.dfuse.get_umount_command(), expect_rc=None)
        self.dfuse.disable_caching.update(False)
        self.dfuse.run()
        # run ior to obtain first read performance after mount
        out = self.run_ior_with_pool(fail_on_warning=False, stop_dfuse=False)
        base_read = IorCommand.get_ior_metrics(out)
        # run ior again to obtain second read performance with caching enabled
        # second read should be multiple times greater than first read
        out = self.run_ior_with_pool(fail_on_warning=False)
        with_caching = IorCommand.get_ior_metrics(out)
        # verifying read performance
        self.assertTrue(float(with_caching[0][max_mib]) >
                        read_x[1] * float(base_read[0][max_mib]))
