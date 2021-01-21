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
        """Run daos_test tests/subtests.

        Test ID: DAOS-1568
        Test Description: Run daos_test tests/subtests.

        Use Cases: core tests for daos_test

        :avocado: tags=all,pr,daily_regression,hw,ib2,medium,daos_test,DAOS_5610
        """
        DaosCoreBase.run_subtest(self)
