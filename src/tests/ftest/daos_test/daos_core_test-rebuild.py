#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from daos_core_base import DaosCoreBase

class DaosCoreTestRebuild(DaosCoreBase):
    # pylint: disable=too-many-ancestors
    """Run just the daos_test rebuild tests.

    :avocado: recursive
    """

    def test_rebuild(self):
        """Jira ID: DAOS-2770.

        Test Description:
            Purpose of this test is to run just the daos_test rebuild tests.

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        """
        self.run_subtest()
