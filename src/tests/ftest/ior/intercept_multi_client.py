"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from ior_intercept_test_base import IorInterceptTestBase


class IorInterceptMultiClient(IorInterceptTestBase):
    """Test class Description: Verify IOR performance with DFUSE + IL is similar to DFS
                               for a single server and multiple client nodes.

    :avocado: recursive
    """

    def test_ior_intercept_libioil(self):
        """Jira ID: DAOS-3499.

        Test Description:
            Verify IOR performance with DFUSE + IL is similar to DFS.

        Use case:
            Run IOR write + read with DFS.
            Run IOR write + read with DFUSE + IL.
            Verify performance with DFUSE + IL is similar to DFS.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daosio,dfuse,il,ior,ior_intercept
        :avocado: tags=IorInterceptMultiClient,test_ior_intercept_libioil
        """
        self.run_il_perf_check('libioil.so')

    def test_ior_intercept_libpil4dfs(self):
        """Jira ID: DAOS-12142.

        Test Description:
            Verify IOR performance with DFUSE + libpil4dfs is similar to DFS.

        Use case:
            Run IOR write + read with DFS.
            Run IOR write + read with DFUSE + libpil4dfs.
            Verify performance with DFUSE + libpil4dfs is similar to DFS.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daosio,dfuse,il,ior,ior_intercept,pil4dfs
        :avocado: tags=IorInterceptMultiClient,test_ior_intercept_libpil4dfs
        """
        self.run_il_perf_check('libpil4dfs.so')
