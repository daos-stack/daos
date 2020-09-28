#!/usr/bin/python
'''
  (C) Copyright 2020 Intel Corporation.

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


class DaosCoreTestRebuildWeekly(DaosCoreBase):
    # pylint: disable=too-many-ancestors
    """Temporarily run the daos_test rebuild tests that take long time in
    weekly.

    Remove this test when DAOS-5717 is closed and enable the tests 18, 22-24 in
    daos_core_test-rebuild.yaml

    :avocado: recursive
    """

    def test_rebuild_weekly(self):
        """JIRA ID: DAOS-5717.

        Test Description:
            Purpose of this test is to run the daos_test rebuild tests that take
            long time in weekly to reduce CI queue.

        Use case:
            Balance testing load between hardware and VM clusters.

        :avocado: tags=all,hw,medium,ib2,unittest,daos_test_rebuild
        :avocado: tags=DAOS-5610,full_regression
        """
        DaosCoreBase.run_subtest(self)
