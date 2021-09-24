#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from file_count_test_base import FileCountTestBase


class LargeFileCount(FileCountTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR and MDTEST to create large number of files.

    :avocado: recursive
    """

    def test_largefilecount(self):
        """Jira ID: DAOS-3845.

        Test Description:
            Run IOR and MDTEST with 30 clients with very large file counts.
            This test is run as part of weekly runs.
        Use Cases:
            Run IOR with DFS and POSIX and create 30 X 10GB files
            Run MDTEST to create 1M files with DFS and POSIX
        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daosio,dfuse
        :avocado: tags=largefilecount
        """
        self.run_file_count()
