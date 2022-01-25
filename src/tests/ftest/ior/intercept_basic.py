#!/usr/bin/python
"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics
from general_utils import percent_change


class IorIntercept(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR with and without interception.

       library on a single server and single client setting with
       basic parameters.

    :avocado: recursive
    """

    def test_ior_intercept(self):
        """Jira ID: DAOS-3498.

        Test Description:
            Purpose of this test is to run ior using dfuse for 5 minutes
            and capture the metrics and use the intercepiton library by
            exporting LD_PRELOAD to the libioil.so path and rerun the
            above ior and capture the metrics and compare the
            performance difference and check using interception
            library make significant performance improvement.

        Use case:
            Run ior with read, write, CheckWrite, CheckRead
                for 5 minutes
            Run ior with read, write, CheckWrite, CheckRead
                for 5 minutes with interception library
            Compare the results and check whether using interception
                library provides better performance.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,small
        :avocado: tags=daosio,dfuse,il
        :avocado: tags=iorinterceptbasic
        """
        # Run IOR without interception
        suffix = self.ior_cmd.transfer_size.value
        out = self.run_ior_with_pool(test_file_suffix=suffix)
        without_intercept = IorCommand.get_ior_metrics(out)

        # Run IOR with interception
        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
        suffix = suffix + "intercept"
        out = self.run_ior_with_pool(intercept, test_file_suffix=suffix)
        with_intercept = IorCommand.get_ior_metrics(out)

        # Index of each metric
        max_mib = int(IorMetrics.Max_MiB)
        mean_mib = int(IorMetrics.Mean_MiB)

        # Write and read performance thresholds
        write_x = self.params.get("write_x", self.ior_cmd.namespace, 1)
        read_x = self.params.get("read_x", self.ior_cmd.namespace, 1)

        # Verify write performance
        # DAOS-5857 & DAOS-9237: Since there is a lot of volatility in the performance results,
        # only check max and mean.
        max_change = percent_change(without_intercept[0][max_mib], with_intercept[0][max_mib])
        self.log.info('assert max_change > write_x: %f > %f', max_change, write_x)
        self.assertGreater(max_change, write_x, "Expected higher max write performance")
        mean_change = percent_change(without_intercept[0][mean_mib], with_intercept[0][mean_mib])
        self.log.info('assert mean_change > write_x: %f > %f', mean_change, write_x)
        self.assertGreater(mean_change, write_x, "Expected higher mean write performance")

        # Verify read performance
        max_change = percent_change(without_intercept[1][max_mib], with_intercept[1][max_mib])
        self.log.info('assert max_change > read_x: %f > %f', max_change, read_x)
        self.assertGreater(max_change, read_x, "Expected higher max read performance")
        mean_change = percent_change(without_intercept[1][mean_mib], with_intercept[1][mean_mib])
        self.log.info('assert mean_change > read_x: %f > %f', mean_change, read_x)
        self.assertGreater(mean_change, read_x, "Expected higher mean read performance")
