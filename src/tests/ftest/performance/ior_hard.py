#!/usr/bin/env python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

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
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_hard,performance_dfs
        :avocado: tags=performance_ior_hard_dfs_sx
        """
        self.run_performance_ior(namespace="/run/ior_dfs_sx/*")

    def test_performance_ior_hard_dfs_ec_16p2gx(self):
        """Test Description: Run IOR Hard, DFS, EC_16P2GX.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_hard,performance_dfs
        :avocado: tags=performance_ior_hard_dfs_16p2gx
        """
        self.run_performance_ior(namespace="/run/ior_dfs_ec_16p2gx/*")

    def test_performance_ior_hard_dfuse_sx(self):
        """Test Description: Run IOR Hard, POSIX dfuse, SX.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_hard,performance_dfuse
        :avocado: tags=performance_ior_hard_dfuse_sx
        """
        self.run_performance_ior(namespace="/run/ior_dfuse_sx/*")

    def test_performance_ior_hard_dfuse_ec_16p2gx(self):
        """Test Description: Run IOR Hard, POSIX dfuse, EC_16P2GX.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_hard,performance_dfuse
        :avocado: tags=performance_ior_hard_dfuse_16p2gx
        """
        self.run_performance_ior(namespace="/run/ior_dfuse_ec_16p2gx/*")
