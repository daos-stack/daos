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
from ior_test_base import IorTestBase
from ior_utils import IorCommand


class IorInterceptVerifyDataIntegrity(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR with mix of dfuse and
       interception library on a multi server and multi client
       settings and verify read/write.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        super(IorInterceptVerifyDataIntegrity, self).setUp()
        # Following line can be removed once the constraint in the
        # IorTestBase is resolved. #DAOS-3320
        self.hostlist_clients = self.params.get(
            "test_clients", "/run/hosts/*")

    def test_ior_intercept_verify_data(self):
        """Jira ID: DAOS-3502.

        Test Description:
            Purpose of this test is to run ior through dfuse with
            interception library  on 5 clients and without interception
            library on 1 client for at least 30 minutes and verify the
            data integrity using ior's Read Verify and Write Verify
            options.

        Use case:
            Run ior with read, write, fpp, read verify
            write verify for 30 minutes
            Run ior with read, write, read verify
            write verify for 30 minutes

        :avocado: tags=all,full_regression,hw,large
        :avocado: tags=daosio,iorinterceptverifydata
        """
        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
        with_intercept = dict()
        self.run_multiple_ior_with_pool(with_intercept, intercept)

        IorCommand.log_metrics(self.log, "5 clients - with " +
                               "interception library", with_intercept[1])
        IorCommand.log_metrics(self.log, "1 client - without " +
                               "interception library", with_intercept[2])
