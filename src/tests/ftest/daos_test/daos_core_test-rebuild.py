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

    # Test variants that should be skipped
    CANCEL_FOR_TICKET = [
        ["DAOS-5851", "test_name", "rebuild tests 0-10"],
    ]

    def test_rebuild(self):
        """Jira ID: DAOS-2770.

        Test Description:
            Purpose of this test is to run just the daos_test rebuild tests.

        Use case:
            Balance testing load between hardware and VM clusters.

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        :avocado: tags=DAOS_5610
        """
        DaosCoreBase.run_subtest(self)
