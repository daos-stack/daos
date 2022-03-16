#!/usr/bin/python3
"""
  (C) Copyright 2021-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
from daos_core_base import DaosCoreBase


class DaosCoreTestDfuse(DaosCoreBase):
    # pylint: disable=too-many-ancestors
    """Runs DAOS DFuse tests.

    :avocado: recursive
    """

    def test_daos_dfuse_openat(self):
        """

        Test Description:
            Run dfuse_test -o

        Use cases:
            DAOS DFuse unit tests

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=dfuse,dfuse_test
        :avocado: tags=dfuse_test_openat
        """
        self.daos_test = os.path.join(self.bin, 'dfuse_test')
        self.run_subtest()
