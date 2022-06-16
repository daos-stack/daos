#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

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
            Run daos_test -N subtest 0

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=unittest,nvme
        :avocado: tags=daos_test,daos_core_test_nvme,test_daos_nvme_recovery_1
        """
        self.run_subtest()

    def test_daos_nvme_recovery_2(self):
        """Jira ID: DAOS-3846.

        Test Description:
            Run daos_test -N subtest 1

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=unittest,nvme
        :avocado: tags=daos_test,daos_core_test_nvme,test_daos_nvme_recovery_2
        """
        self.run_subtest()

    def test_daos_nvme_recovery_3(self):
        """Jira ID: DAOS-3846.

        Test Description:
            Run daos_test -N subtest 2

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=unittest,nvme
        :avocado: tags=daos_test,daos_core_test_nvme,test_daos_nvme_recovery_3
        """
        self.run_subtest()

    def test_daos_nvme_recovery_4(self):
        """Jira ID: DAOS-3846.

        Test Description:
            Run daos_test -N subtest 3

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=unittest,nvme
        :avocado: tags=daos_test,daos_core_test_nvme,test_daos_nvme_recovery_4
        """
        self.run_subtest()

    def test_daos_nvme_recovery_5(self):
        """Jira ID: DAOS-3760.

        Test Description:
            Run daos_test -N subtest 4

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=unittest,nvme
        :avocado: tags=daos_test,daos_core_test_nvme,test_daos_nvme_recovery_5
        """
        self.run_subtest()

    def test_daos_nvme_recovery_6(self):
        """Jira ID: DAOS-7120.
        Test Description:
            Run daos_test -N subtest 5
        Use cases:
            daos_test NVMe recovery test
        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=unittest,nvme
        :avocado: tags=daos_test,daos_core_test_nvme,test_daos_nvme_recovery_6
        """
        self.run_subtest()
