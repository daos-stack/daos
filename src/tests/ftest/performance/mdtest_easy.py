#!/usr/bin/env python3
'''
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from performance_test_base import PerformanceTestBase

class MdtestEasy(PerformanceTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Run MDTest Easy

    :avocado: recursive
    """

    def test_performance_mdtest_easy_dfs_s1(self):
        """Test Description: Run MDTest Easy, DFS, S1.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_mdtest,performance_mdtest_easy,performance_dfs
        :avocado: tags=performance_mdtest_easy_dfs_s1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfs_s1/*")

    def test_performance_mdtest_easy_dfs_ec_16p2g1(self):
        """Test Description: Run MDTest Easy, DFS, EC_16P2G1.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_mdtest,performance_mdtest_easy,performance_dfs
        :avocado: tags=performance_mdtest_easy_dfs_ec_16p2g1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfs_ec_16p2g1/*")

    def test_performance_mdtest_easy_dfs_ec_4p2g1_stop(self):
        """Test Description: Run MDTest Easy, DFS, EC_4P2G1, stop a rank.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_mdtest,performance_mdtest_easy,performance_dfs
        :avocado: tags=performance_mdtest_easy_dfs_ec_4p2g1_stop
        """
        self.run_performance_mdtest(
            namespace="/run/mdtest_dfs_ec_4p2g1/*",
            stop_delay=0.5)

    def test_performance_mdtest_easy_dfs_ec_16p2g1_stop(self):
        """Test Description: Run MDTest Easy, DFS, EC_16P2G1, stop a rank.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_mdtest,performance_mdtest_easy,performance_dfs
        :avocado: tags=performance_mdtest_easy_dfs_ec_16p2g1_stop
        """
        self.run_performance_mdtest(
            namespace="/run/mdtest_dfs_ec_16p2g1/*",
            stop_delay=0.5)
