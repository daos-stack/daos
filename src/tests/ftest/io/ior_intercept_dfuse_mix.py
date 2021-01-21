#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics


class IorInterceptDfuseMix(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR only with dfuse and with mix of
       dfuse and interception library on a single server and multi
       client settings with basic parameters.

    :avocado: recursive
    """

    def test_ior_intercept_dfuse_mix(self):
        """Jira ID: DAOS-3500.

        Test Description:
            Purpose of this test is to run ior through dfuse on 4 clients
            for 5 minutes and capture the metrics and use the
            intercepiton library by exporting LD_PRELOAD to the libioil.so
            path on 3 clients and leave 1 client to use dfuse and rerun
            the above ior and capture the metrics and compare the
            performance difference and check using interception
            library make significant performance improvement. Verify the
            client didn't use the interception library doesn't show any
            improvement.

        Use case:
            Run ior with read, write for 5 minutes
            Run ior with read, write for 5 minutes with interception
            library

            Compare the results and check whether using interception
                library provides better performance and not using it
                does not change the performance.

        :avocado: tags=all,full_regression,hw,large,daosio,iorinterceptmix
        """
        without_intercept = dict()
        self.run_multiple_ior_with_pool(without_intercept)
        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
        with_intercept = dict()
        self.run_multiple_ior_with_pool(with_intercept, intercept)
        self.log_metrics(without_intercept, with_intercept)

        max_mib = int(IorMetrics.Max_MiB)
        min_mib = int(IorMetrics.Min_MiB)
        mean_mib = int(IorMetrics.Mean_MiB)

        write_x = self.params.get("write_x", "/run/ior/iorflags/ssf/*", 1)
        read_x = self.params.get("read_x", "/run/ior/iorflags/ssf/*", 1)

        # Verify that using interception library gives desired performance
        # improvement.
        # Verifying write performance
        self.assertTrue(float(with_intercept[1][0][max_mib]) >
                        write_x * float(without_intercept[1][0][max_mib]))
        self.assertTrue(float(with_intercept[1][0][min_mib]) >
                        write_x * float(without_intercept[1][0][min_mib]))
        self.assertTrue(float(with_intercept[1][0][mean_mib]) >
                        write_x * float(without_intercept[1][0][mean_mib]))

        # Verifying read performance
        self.assertTrue(float(with_intercept[1][1][max_mib]) >
                        read_x * float(without_intercept[1][1][max_mib]))
        self.assertTrue(float(with_intercept[1][1][min_mib]) >
                        read_x * float(without_intercept[1][1][min_mib]))
        self.assertTrue(float(with_intercept[1][1][mean_mib]) >
                        read_x * float(without_intercept[1][1][mean_mib]))

        # Verify that not using interception library on both runs does
        # not change the performance.
        # Perf. improvement if any is less than the desired.
        # Verifying write performance
        self.assertTrue(float(with_intercept[2][0][max_mib]) <
                        write_x * float(without_intercept[2][0][max_mib]))
        self.assertTrue(float(with_intercept[2][0][min_mib]) <
                        write_x * float(without_intercept[2][0][min_mib]))
        self.assertTrue(float(with_intercept[2][0][mean_mib]) <
                        write_x * float(without_intercept[2][0][mean_mib]))

        # Verifying read performance
        # Read performance is not significant with interception library
        # and most likely the read_x will be 1. To avoid unnecessary
        # failure keeping flat 1.5 x just to set the boundary for the client
        # without interception library
        self.assertTrue(float(with_intercept[2][1][max_mib]) <
                        1.5 * float(without_intercept[2][1][max_mib]))
        self.assertTrue(float(with_intercept[2][1][min_mib]) <
                        1.5 * float(without_intercept[2][1][min_mib]))
        self.assertTrue(float(with_intercept[2][1][mean_mib]) <
                        1.5 * float(without_intercept[2][1][mean_mib]))

    def log_metrics(self, without_intercept, with_intercept):
        """Log the ior metrics because the stdout from ior can be mixed
           because of multithreading.

           Args:
               without_intercept (dict): IOR Metrics without using
                                         interception library.
               with_intercept (dict): IOR Metrics using interception
                                      library.
        """
        IorCommand.log_metrics(self.log, "3 clients - without " +
                               "interception library", without_intercept[1])
        IorCommand.log_metrics(self.log, "3 clients - with " +
                               "interception library", with_intercept[1])
        IorCommand.log_metrics(self.log, "1 client - without " +
                               "interception library", without_intercept[2])
        IorCommand.log_metrics(self.log, "1 clients - without " +
                               "interception library", with_intercept[2])
