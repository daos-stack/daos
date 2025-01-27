"""
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from daos_core_base import DaosCoreBase


class DaosCoreTestDfs(DaosCoreBase):
    """Runs DAOS File System (DFS) tests.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize the DaosCoreBase object."""
        super().__init__(*args, **kwargs)
        self.hostfile_clients_slots = None

    def test_daos_dfs_unit(self):
        """Jira ID: DAOS-5409.

        Test Description:
            Run dfs_test -u

        Use cases:
            DAOS File System unit tests

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=daos_test,dfs_test,dfs
        :avocado: tags=DaosCoreTestDfs,test_daos_dfs_unit
        """
        self.run_subtest(os.path.join(self.bin, "dfs_test"))

    def test_daos_dfs_parallel(self):
        """Jira ID: DAOS-5409.

        Test Description:
            Run dfs_test -p

        Use cases:
            DAOS File System parallel unit tests

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=daos_test,dfs_test,dfs
        :avocado: tags=DaosCoreTestDfs,test_daos_dfs_parallel
        """
        self.run_subtest(os.path.join(self.bin, "dfs_test"))

    def test_daos_dfs_sys(self):
        """Jira ID: DAOS-7759.

        Test Description:
            Run dfs_test -s

        Use cases:
            DAOS File System sys unit tests

        :avocado: tags=all,pr,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daos_test,dfs_test,dfs
        :avocado: tags=DaosCoreTestDfs,test_daos_dfs_sys
        """
        self.run_subtest(os.path.join(self.bin, "dfs_test"))
