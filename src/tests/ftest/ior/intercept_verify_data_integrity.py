#!/usr/bin/python
"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
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

    def test_ior_intercept_verify_data(self):
        """Jira ID: DAOS-3502.

        Test Description:
            Purpose of this test is to run ior through dfuse with
            interception library on 5 clients and without interception
            library on 1 client for at least 30 minutes and verify the
            data integrity using ior's Read Verify and Write Verify
            options.

        Use case:
            Run ior with read, write, fpp, read verify
            write verify for 30 minutes
            Run ior with read, write, read verify
            write verify for 30 minutes

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daosio,dfuse,il,ior_intercept
        :avocado: tags=ior_intercept_verify_data
        """
        self.add_pool()
        self.add_container(self.pool)

        intercept = os.path.join(self.prefix, 'lib64', 'libioil.so')
        results = dict()
        client_count = len(self.hostlist_clients)
        w_clients = self.hostlist_clients[0:client_count - 1]
        wo_clients = [self.hostlist_clients[-1]]

        self.run_ior_threads_il(
            results=results, intercept=intercept, with_clients=w_clients,
            without_clients=wo_clients)

        IorCommand.log_metrics(
            self.log, "5 clients - with interception library", results[1])
        IorCommand.log_metrics(
            self.log, "1 client - without interception library", results[2])
