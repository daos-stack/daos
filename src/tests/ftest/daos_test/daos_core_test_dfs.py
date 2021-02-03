#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from daos_core_base import DaosCoreBase

class DaosCoreTestDfs(DaosCoreBase):
    """Runs DAOS file system (DFS) tests.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize the DaosCoreBase object."""
        super(DaosCoreTestDfs, self).__init__(*args, **kwargs)
        self.hostfile_clients_slots = None

    def test_daos_dfs_unit(self):
        """Jira ID: DAOS-5409

        Test Description:
            Run daos_test -u

        Use cases:
            Daos File system tests

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=dfs_test
        :avocado: tags=daos_dfs_unit
        """
        self.daos_test = os.path.join(self.bin, 'dfs_test')
        self.run_subtest()

    def test_daos_dfs_parallel(self):
        """Jira ID: DAOS-5409

        Test Description:
            Run daos_test -p

        Use cases:
            Daos File system tests

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=dfs_test
        :avocado: tags=daos_dfs_parallel
        """
        self.daos_test = os.path.join(self.bin, 'dfs_test')
        self.run_subtest()
