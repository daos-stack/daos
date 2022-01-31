#!/usr/bin/env python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from performance_test_base import PerformanceTestBase

class IorEasy(PerformanceTestBase):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-few-public-methods
    """Test class Description: Run IOR Easy

    Use Cases:
            Create a pool, container, and run IOR Easy.

    :avocado: recursive
    """

    def test_performance_ior_easy_dfs_sx(self):
        """Test Description: Run IOR Easy, DFS, SX.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_easy,performance_dfs
        :avocado: tags=performance_ior_easy_dfs_sx
        """
        self.run_performance_ior(namespace="/run/ior_dfs_sx/*")

    def test_performance_ior_easy_dfs_ec_16p2gx(self):
        """Test Description: Run IOR Easy, DFS, EC_16P2GX.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_easy,performance_dfs
        :avocado: tags=performance_ior_easy_dfs_ec_16p2gx
        """
        self.run_performance_ior(namespace="/run/ior_dfs_ec_16p2gx/*")

    def test_performance_ior_easy_dfuse_sx(self):
        """Test Description: Run IOR Easy, POSIX dfuse, SX.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_easy,performance_dfuse
        :avocado: tags=performance_ior_easy_dfuse_sx
        """
        self.run_performance_ior(namespace="/run/ior_dfuse_sx/*")

    def test_performance_ior_easy_dfuse_ec_16p2gx(self):
        """Test Description: Run IOR Easy, POSIX dfuse, EC_16P2GX.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_easy,performance_dfuse
        :avocado: tags=performance_ior_easy_dfuse_16p2gx
        """
        self.run_performance_ior(namespace="/run/ior_dfuse_ec_16p2gx/*")

    def test_performance_ior_easy_dfs_ec_4p2gx_stop_write(self):
        """Test Description: Run IOR Easy, DFS, EC_4P2GX, stop a rank during write

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_easy,performance_dfs
        :avocado: tags=performance_ior_easy_dfs_ec_4p2gx_stop_write
        """
        self.run_performance_ior(
            namespace="/run/ior_dfs_ec_4p2gx/*",
            stop_delay_write=0.5)

    def test_performance_ior_easy_dfs_ec_4p2gx_stop_read(self):
        """Test Description: Run IOR Easy, DFS, EC_4P2GX, stop a rank during read.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_easy,performance_dfs
        :avocado: tags=performance_ior_easy_dfs_ec_4p2gx_stop_read
        """
        self.run_performance_ior(
            namespace="/run/ior_dfs_ec_4p2gx/*",
            stop_delay_read=0.5)

    def test_performance_ior_easy_dfs_ec_16p2gx_stop_write(self):
        """Test Description: Run IOR Easy, DFS, EC_16P2GX, stop a rank during write.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_easy,performance_dfs
        :avocado: tags=performance_ior_easy_dfs_ec_16p2gx_stop_write
        """
        self.run_performance_ior(
            namespace="/run/ior_dfs_ec_16p2gx/*",
            stop_delay_write=0.5)

    def test_performance_ior_easy_dfs_ec_16p2gx_stop_read(self):
        """Test Description: Run IOR Easy, DFS, EC_16P2GX, stop a rank during read.

        :avocado: tags=all,manual
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_easy,performance_dfs
        :avocado: tags=performance_ior_easy_dfs_ec_16p2gx_stop_read
        """
        self.run_performance_ior(
            namespace="/run/ior_dfs_ec_16p2gx/*",
            stop_delay_read=0.5)

    def test_performance_ior_easy_hdf5_sx(self):
        """Test Description: Run IOR Easy, HDF5, SX.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_easy,performance_hdf5
        :avocado: tags=performance_ior_easy_hdf5_sx
        """
        self.run_performance_ior(namespace="/run/ior_hdf5_sx/*")

    def test_performance_ior_easy_mpiio_sx(self):
        """Test Description: Run IOR Easy, MPIIO, SX.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=performance,performance_ior,performance_ior_easy,performance_mpiio
        :avocado: tags=performance_ior_easy_mpiio_sx
        """
        self.run_performance_ior(namespace="/run/ior_mpiio_sx/*")
