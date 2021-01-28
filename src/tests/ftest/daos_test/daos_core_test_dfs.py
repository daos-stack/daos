#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
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
