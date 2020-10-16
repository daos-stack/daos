#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
"""

import os
from apricot import skipForTicket
from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics


class IorIntercept(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR with and without interception
       library on a single server and single client setting with
       basic parameters.

    :avocado: recursive
    """

    @skipForTicket("DAOS-5857")
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

        :avocado: tags=all,full_regression,hw,small,daosio,iorinterceptbasic
        """
        apis = self.params.get("ior_api", '/run/ior/iorflags/ssf/*')
        for api in apis:
            self.ior_cmd.api.update(api)
            out = self.run_ior_with_pool(fail_on_warning=False)
            without_intercept = IorCommand.get_ior_metrics(out)
            if api == "POSIX":
                intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
                out = self.run_ior_with_pool(intercept, fail_on_warning=False)
                with_intercept = IorCommand.get_ior_metrics(out)
                max_mib = int(IorMetrics.Max_MiB)
                min_mib = int(IorMetrics.Min_MiB)
                mean_mib = int(IorMetrics.Mean_MiB)
                write_x = self.params.get("write_x",
                                          "/run/ior/iorflags/ssf/*", 1)
                read_x = self.params.get("read_x",
                                         "/run/ior/iorflags/ssf/*", 1)

                # Verifying write performance
                self.assertTrue(float(with_intercept[0][max_mib]) >
                                write_x * float(without_intercept[0][max_mib]))
                self.assertTrue(float(with_intercept[0][min_mib]) >
                                write_x * float(without_intercept[0][min_mib]))
                self.assertTrue(float(with_intercept[0][mean_mib]) >
                                write_x * float(without_intercept[0][mean_mib]))

                # Verifying read performance
                self.assertTrue(float(with_intercept[1][max_mib]) >
                                read_x * float(without_intercept[1][max_mib]))
                self.assertTrue(float(with_intercept[1][min_mib]) >
                                read_x * float(without_intercept[1][min_mib]))
                self.assertTrue(float(with_intercept[1][mean_mib]) >
                                read_x * float(without_intercept[1][mean_mib]))
