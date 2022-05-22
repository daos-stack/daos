#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.
  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from file_count_test_base import FileCountTestBase


class SmallFileCount(FileCountTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs IOR and MDTEST to create large number of files.

    :avocado: recursive
    """

    def test_smallfilecount(self):
        """Jira ID: DAOS-3845.

        Test Description:
            Run IOR and MDTEST with 30 clients with smaller file counts.
        Use Cases:
            Run IOR for 5 mints with DFS and POSIX and create 30 x 2G files
            Run MDTEST to create 50K files with DFS and POSIX
        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=daosio,dfuse
        :avocado: tags=smallfilecount
        """
        self.run_file_count()
