"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from daos_core_base import DaosCoreBase


class IncReintContRecovTest(DaosCoreBase):
    # pylint: disable=too-many-ancestors,too-many-public-methods
    """Runs daos incremental reintegration core tests.

    :avocado: recursive
    """

    def test_daos_inc_reint_cont_recov(self):
        """Jira ID: DAOS-17857

        Test Description:
            Run daos_test -Y

        Use cases:
            Core tests for daos container recovery when incremental reintegration

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium,provider
        :avocado: tags=inc_reint,cont_recov
        :avocado: tags=IncReintContRecovTest,test_daos_inc_reint_cont_recov
        """
        self.run_subtest()
