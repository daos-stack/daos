'''
  (C) Copyright 2019-2023 Intel Corporation.

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
        :avocado: tags=hw,medium
        :avocado: tags=performance,performance_mdtest,performance_mdtest_hard,performance_dfs
        :avocado: tags=MdtestHard,test_performance_mdtest_hard_dfs_s1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfs_s1/*")

    def test_performance_mdtest_hard_dfs_ec_16p2g1(self):
        """Test Description: Run MdTest Hard, DFS, EC_16P2G1.

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=performance,performance_mdtest,performance_mdtest_hard,performance_dfs
        :avocado: tags=MdtestHard,test_performance_mdtest_hard_dfs_ec_16p2g1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfs_ec_16p2g1/*")

    def test_performance_mdtest_hard_dfuse_il_s1(self):
        """Test Description: Run MdTest Hard, POSIX+IL, S1.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=performance,performance_mdtest,performance_mdtest_hard,performance_dfuse
        :avocado: tags=MdtestHard,test_performance_mdtest_hard_dfuse_il_s1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfuse_il_s1/*")

    def test_performance_mdtest_hard_dfuse_pil4dfs_s1(self):
        """Test Description: Run MdTest Hard, POSIX+PIL4DFS, S1.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=performance,performance_mdtest,performance_mdtest_hard,performance_dfuse
        :avocado: tags=MdtestHard,test_performance_mdtest_hard_dfuse_pil4dfs_s1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfuse_pil4dfs_s1/*")

    def test_performance_mdtest_hard_dfuse_pil4dfs_ec_16p2g1(self):
        """Test Description: Run MdTest Hard, POSIX+PIL4DFS, EC_16P2G1.

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=performance,performance_mdtest,performance_mdtest_hard,performance_dfuse
        :avocado: tags=MdtestHard,test_performance_mdtest_hard_dfuse_pil4dfs_ec_16p2g1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfuse_pil4dfs_ec_16p2g1/*")

    def test_performance_mdtest_hard_dfuse_il_ec_16p2g1(self):
        """Test Description: Run MdTest Hard, POSIX+IL, EC_16P2G1.

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=performance,performance_mdtest,performance_mdtest_hard,performance_dfuse
        :avocado: tags=MdtestHard,test_performance_mdtest_hard_dfuse_il_ec_16p2g1
        """
        self.run_performance_mdtest(namespace="/run/mdtest_dfuse_il_ec_16p2g1/*")
