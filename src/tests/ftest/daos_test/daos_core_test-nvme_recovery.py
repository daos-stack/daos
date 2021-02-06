#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from daos_core_base import DaosCoreBase

class DaosCoreTestNvme(DaosCoreBase):
    # pylint: disable=too-many-ancestors
    """Run just the daos_test NVMe Recovery tests.

    :avocado: recursive
    """

    def test_nvme(self):
        """Jira ID: DAOS-3846.

        Test Description:
            Purpose of this test is to run just the daos_test NVMe recovery
            tests

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=nvme
        :avocado: tags=daos_nvme_recovery,unittest
        """
        self.run_subtest()
