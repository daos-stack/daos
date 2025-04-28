'''
  (C) Copyright 2019-2024 Intel Corporation.

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
        :avocado: tags=hw,medium
        :avocado: tags=performance
        :avocado: tags=MdtestEasy,test_performance_mdtest_easy_dfs_s1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfs_s1/*")

    def test_performance_mdtest_easy_dfs_ec_16p2g1(self):
        """Test Description: Run MDTest Easy, DFS, EC_16P2G1.

        :avocado: tags=all,manual
        :avocado: tags=performance
        :avocado: tags=MdtestEasy,test_performance_mdtest_easy_dfs_ec_16p2g1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfs_ec_16p2g1/*")

    def test_performance_mdtest_easy_dfs_rp_3g1(self):
        """Test Description: Run MDTest Easy, DFS, RP_3G1.

        :avocado: tags=all,manual
        :avocado: tags=performance
        :avocado: tags=MdtestEasy,test_performance_mdtest_easy_dfs_rp_3g1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfs_rp_3g1/*")

    def test_performance_mdtest_easy_pil4dfs_s1(self):
        """Test Description: Run MDTest Easy, dfuse + pil4dfs, S1.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=performance
        :avocado: tags=MdtestEasy,test_performance_mdtest_easy_pil4dfs_s1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_pil4dfs_s1/*")

    def test_performance_mdtest_easy_pil4dfs_ec_16p2g1(self):
        """Test Description: Run MDTest Easy, dfuse + pil4dfs, EC_16P2G1.

        :avocado: tags=all,manual
        :avocado: tags=performance
        :avocado: tags=MdtestEasy,test_performance_mdtest_easy_pil4dfs_ec_16p2g1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_pil4dfs_ec_16p2g1/*")

    def test_performance_mdtest_easy_pil4dfs_rp_3g1(self):
        """Test Description: Run MDTest Easy, dfuse + pil4dfs, RP_3G1.

        :avocado: tags=all,manual
        :avocado: tags=performance
        :avocado: tags=MdtestEasy,test_performance_mdtest_easy_pil4dfs_rp_3g1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_pil4dfs_rp_3g1/*")
