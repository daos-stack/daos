#!/usr/bin/env python3
'''
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from performance_test_base import PerformanceTestBase

class MdtestHard(PerformanceTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Run MdTest Hard

    :avocado: recursive
    """

    def test_performance_mdtest_hard_dfs_s1(self):
        """Test Description: Run MdTest Hard, DFS, S1.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_mdtest,performance_mdtest_hard,performance_dfs
        :avocado: tags=performance_mdtest_hard_dfs_s1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfs_s1/*")

    def test_performance_mdtest_hard_dfs_ec_16p2g1(self):
        """Test Description: Run MdTest Hard, DFS, EC_16P2G1.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_mdtest,performance_mdtest_hard,performance_dfs
        :avocado: tags=performance_mdtest_hard_dfs_ec_16p2g1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfs_ec_16p2g1/*")
