"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from daos_core_base import DaosCoreBase


class CatRecovCoreTest(DaosCoreBase):
    # pylint: disable=too-many-ancestors,too-many-public-methods
    """Runs daos catastrophic recovery core tests.

    :avocado: recursive
    """

    def test_daos_cat_recov_core(self):
        """Jira ID: DAOS-13047

        Test Description:
            Run daos_test -F

        Use cases:
            Core tests for daos catastrophic recovery

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=recovery,cat_recov
        :avocado: tags=CatRecovCoreTest,test_daos_cat_recov_core
        """
        self.run_subtest()
