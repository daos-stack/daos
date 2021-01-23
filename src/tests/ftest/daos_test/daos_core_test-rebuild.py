#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

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
from apricot import skipForTicket

class DaosCoreTestRebuild(DaosCoreBase):
    # pylint: disable=too-many-ancestors
    """Run just the daos_test rebuild tests.

    :avocado: recursive
    """

    @skipForTicket("DAOS-5851")
    def test_rebuild_0to10(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=0-10

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_0to10
        """
        self.run_subtest(self)

    def test_rebuild_12to15(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=12-15

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_12to15
        """
        self.run_subtest(self)

    def test_rebuild_16(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=16

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_16
        """
        self.run_subtest(self)

    def test_rebuild_17(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=17

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_17
        """
        self.run_subtest(self)

    def test_rebuild_18(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=18

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_18
        """
        self.run_subtest(self)

    def test_rebuild_19(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=19

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_19
        """
        self.run_subtest(self)

    def test_rebuild_20(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=20

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_20
        """
        self.run_subtest(self)

    def test_rebuild_21(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=21

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_21
        """
        self.run_subtest(self)

    def test_rebuild_22(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=22

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_22
        """
        self.run_subtest(self)

    def test_rebuild_23(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=23

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_23
        """
        self.run_subtest(self)

    def test_rebuild_24(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=24

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_24
        """
        self.run_subtest(self)

    def test_rebuild_25(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=25

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_25
        """
        self.run_subtest(self)

    def test_rebuild_26(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=26

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_26
        """
        self.run_subtest(self)

    def test_rebuild_27(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=27

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_27
        """
        self.run_subtest(self)

    def test_rebuild_28(self):
        """Jira ID: DAOS-2770

        Test Description:
            Run daos_test -r -s3 -u subtests=28

        Use cases:
            Core tests for daos_test rebuild

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,ib2,medium
        :avocado: tags=daos_test_rebuild,unittest
        :avocado: tags=daos_rebuild_28
        """
        self.run_subtest(self)
