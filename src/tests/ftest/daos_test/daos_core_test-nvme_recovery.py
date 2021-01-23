#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
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
