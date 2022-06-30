#!/usr/bin/python
"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from ior_intercept_test_base import IorInterceptTestBase


class IorInterceptBasic(IorInterceptTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify IOR performance with DFUSE + IL is similar to DFS
                               for a single server and single client node.

    :avocado: recursive
    """

    def test_ior_intercept(self):
        """Jira ID: DAOS-3498.

        Test Description:
            Verify IOR performance with DFUSE + IL is similar to DFS.

        Use case:
            Run IOR write + read with DFS.
            Run IOR write + read with DFUSE + IL.
            Verify performance with DFUSE + IL is similar to DFS.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,small
        :avocado: tags=daosio,dfuse,il,ior,ior_intercept
        :avocado: tags=ior_intercept_basic
        """
        self.run_il_perf_check()

