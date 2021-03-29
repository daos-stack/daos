#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from apricot import skipForTicket
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
        :avocado: tags=unittest,nvme
        :avocado: tags=daos_test,daos_core_test_nvme,test_daos_nvme_recovery_1
        """
        self.run_subtest()

    @skipForTicket("DAOS-5134")
    def test_daos_nvme_recovery_2(self):
        """Jira ID: DAOS-3846.

        Test Description:
            Run daos_test -N subtest 2

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=unittest,nvme
        :avocado: tags=daos_test,daos_core_test_nvme,test_daos_nvme_recovery_2
        """
        self.run_subtest()

    @skipForTicket("DAOS-5134")
    def test_daos_nvme_recovery_3(self):
        """Jira ID: DAOS-3846.

        Test Description:
            Run daos_test -N subtest 3

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=unittest,nvme
        :avocado: tags=daos_test,daos_core_test_nvme,test_daos_nvme_recovery_3
        """
        self.run_subtest()

    @skipForTicket("DAOS-5134")
    def test_daos_nvme_recovery_4(self):
        """Jira ID: DAOS-3846.

        Test Description:
            Run daos_test -N subtest 4

        Use cases:
            daos_test NVMe recovery test

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=unittest,nvme
        :avocado: tags=daos_test,daos_core_test_nvme,test_daos_nvme_recovery_4
        """
        self.run_subtest()
