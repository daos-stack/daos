#!/usr/bin/python
"""
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics
import write_host_file


class IorInterceptMultiClient(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR with and without interception
       library on a single server and multi client settings with
       basic parameters.

    :avocado: recursive
    """

    def test_ior_intercept_multi_client(self):
        """Jira ID: DAOS-3499.

        Test Description:
            Purpose of this test is to run ior through dfuse in multiple
            clients for 5 minutes and capture the metrics and use the
            intercepiton library by exporting LD_PRELOAD to the libioil.so
            path and rerun the above ior and capture the metrics and
            compare the performance difference and check using interception
            library make significant performance improvement.

        Use case:
            Run ior with read, write for 5 minutes
            Run ior with read, write for 5 minutes with interception library
            Compare the results and check whether using interception
                library provides better performance.

        :avocado: tags=all,full_regression,hw,large
        :avocado: tags=daosio,iorinterceptmulticlient
        """
        suffix = self.ior_cmd.transfer_size.value
        out = self.run_ior_with_pool(test_file_suffix=suffix)
        without_intercept = IorCommand.get_ior_metrics(out)
        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
        suffix = suffix + "intercept"
        out = self.run_ior_with_pool(intercept, test_file_suffix=suffix)
        with_intercept = IorCommand.get_ior_metrics(out)
        max_mib = int(IorMetrics.Max_MiB)
        min_mib = int(IorMetrics.Min_MiB)
        mean_mib = int(IorMetrics.Mean_MiB)

        write_x = self.params.get("write_x", "/run/ior/iorflags/ssf/*", 1)

        # Verifying write performance
        self.assertTrue(float(with_intercept[0][max_mib]) >
                        write_x * float(without_intercept[0][max_mib]))
        self.assertTrue(float(with_intercept[0][min_mib]) >
                        write_x * float(without_intercept[0][min_mib]))
        self.assertTrue(float(with_intercept[0][mean_mib]) >
                        write_x * float(without_intercept[0][mean_mib]))

        # Verifying read performance
        # The read performance is almost same with or without intercept
        # library. But arbitrarily the read performance with interception
        # library can be bit lower than without it. Verifying that it is
        # not drastically lower by checking it is at least  60% or above.
        read_x = 0.6
        self.assertTrue(float(with_intercept[1][max_mib]) >
                        read_x * float(without_intercept[1][max_mib]))
        self.assertTrue(float(with_intercept[1][min_mib]) >
                        read_x * float(without_intercept[1][min_mib]))
        self.assertTrue(float(with_intercept[1][mean_mib]) >
                        read_x * float(without_intercept[1][mean_mib]))
