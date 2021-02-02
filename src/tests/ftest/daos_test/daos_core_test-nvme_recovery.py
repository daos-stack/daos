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

    def test_daos_nvme_recovery_1(self):
        """Jira ID: DAOS-3846.

        Test Description:
            Run daos_test -N subtest 1

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=nvme
        :avocado: tags=daos_nvme_recovery_1,unittest
        """
        self.run_subtest()

    def test_daos_nvme_recovery_2(self):
        """Jira ID: DAOS-3846.

        Test Description:
            Run daos_test -N subtest 2

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=nvme
        :avocado: tags=daos_nvme_recovery_2,unittest
        """
        self.run_subtest()

    def test_daos_nvme_recovery_3(self):
        """Jira ID: DAOS-3846.

        Test Description:
            Run daos_test -N subtest 3

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=nvme
        :avocado: tags=daos_nvme_recovery_3,unittest
        """
        self.run_subtest()

    def test_daos_nvme_recovery_4(self):
        """Jira ID: DAOS-3846.

        Test Description:
            Run daos_test -N subtest 4

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=nvme
        :avocado: tags=daos_nvme_recovery_4,unittest
        """
        self.run_subtest()
