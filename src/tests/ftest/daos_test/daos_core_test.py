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

    def test_daos_pool(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -p

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_pool
        """
        self.run_subtest()

    def test_daos_container(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -c

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_container
        """
        self.run_subtest()

    def test_daos_epoch(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -e

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_epoch
        """
        self.run_subtest()

    def test_daos_single_rdg_tx(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -t

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_single_rdg_tx
        """
        self.run_subtest()

    def test_daos_distributed_tx(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -T

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_distributed_tx
        """
        self.run_subtest()

    def test_daos_verify_consistency(self):
        """Jira ID: DAOS-1568

        Test Description:
            Run daos_test -V

        Use cases:
            Core tests for daos_test

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test
        :avocado: tags=daos_verify_consistency
        """
        self.run_subtest()
