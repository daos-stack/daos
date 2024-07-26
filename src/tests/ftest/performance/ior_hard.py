"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from performance_test_base import PerformanceTestBase


class IorHard(PerformanceTestBase):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-few-public-methods
    """Test class Description: Run IOR Hard

    Use Cases:
            Create a pool, container, and run IOR Hard.

    :avocado: recursive
    """

    def test_performance_ior_hard_dfs_sx(self):
        """Test Description: Run IOR Hard, DFS, SX.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=performance
        :avocado: tags=IorHard,test_performance_ior_hard_dfs_sx
        """
        self.run_performance_ior(namespace="/run/ior_dfs_sx/*")

    def test_performance_ior_hard_dfs_ec_16p2gx(self):
        """Test Description: Run IOR Hard, DFS, EC_16P2GX.

        :avocado: tags=all,manual
        :avocado: tags=performance
        :avocado: tags=IorHard,test_performance_ior_hard_dfs_ec_16p2gx
        """
        self.run_performance_ior(namespace="/run/ior_dfs_ec_16p2gx/*")
