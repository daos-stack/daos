#!/usr/bin/env python3
'''
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from performance_test_base import PerformanceTestBase

class MdtestHard(PerformanceTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Run MdTest Hard

    :avocado: recursive
    :avocado: tags=performance,performance_mdtest,performance_mdtest_hard
    """

    def test_performance_mdtest_hard_dfs_sx(self):
        """Test Description: Run MdTest Hard, DFS, SX.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=performance_dfs
        :avocado: tags=performance_mdtest_hard_dfs_sx
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfs_sx/*")

    def test_performance_mdtest_hard_dfs_ec_16p2gx(self):
        """Test Description: Run MdTest Hard, DFS, EC_16P2GX.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance_dfs
        :avocado: tags=performance_mdtest_hard_dfs_ec_16p2gx
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfs_ec_16p2gx/*")
