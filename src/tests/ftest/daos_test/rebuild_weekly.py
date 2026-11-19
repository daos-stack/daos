'''
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from daos_core_base import DaosCoreBase


class DaosCoreTestRebuildWeekly(DaosCoreBase):
    """Run the long daos_test rebuild tests (daos_test -k) weekly.

    :avocado: recursive
    """

    def test_rebuild_weekly(self):
        """Jira ID: DAOS-19842

        Test Description:
            Run daos_test -k -s5: all the REBUILD_WEEKLY sub-tests of
            src/tests/suite/daos_rebuild_weekly.c, which are too slow to run
            on every PR.

        Use cases:
            Long-running rebuild tests for daos_test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=unittest,rebuild
        :avocado: tags=DaosCoreTestRebuildWeekly,daos_test,daos_core_test_rebuild_weekly
        :avocado: tags=test_rebuild_weekly
        """
        self.run_subtest()
