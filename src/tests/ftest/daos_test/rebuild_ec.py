"""
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from daos_core_base import DaosCoreBase


class DaosCoreTestRebuildEC(DaosCoreBase):
    """Run the daos_test EC rebuild tests that need more than 8 engines.

    :avocado: recursive
    """

    def test_rebuild_ec_49(self):
        """Jira ID: DAOS-19632

        Test Description:
            Run daos_test -S -s5 -u subtests=49 on 10 engines

        Use cases:
            EC reintegration data loss through parity replica epoch inflation

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=daos_test,daos_core_test_rebuild_ec,rebuild,ec
        :avocado: tags=DaosCoreTestRebuildEC,test_rebuild_ec_49
        """
        self.run_subtest()
