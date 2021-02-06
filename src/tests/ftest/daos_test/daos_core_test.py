#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from daos_core_base import DaosCoreBase


class DaosCoreTest(DaosCoreBase):
    # pylint: disable=too-many-ancestors
    """Runs just the non-rebuild daos_test tests.

    :avocado: recursive
    """

    def test_subtest(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test tests/subtests

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        """
        self.run_subtest()
