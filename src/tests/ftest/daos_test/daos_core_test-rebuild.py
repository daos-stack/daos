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

       Jira ID: DAOS-2770.

       Test Description:
            Purpose of this test is to run just the daos_test rebuild tests.

       Use case:
            Balance testing load between hardware and VM clusters.

    :avocado: recursive
    """

    @skipForTicket("DAOS-5851")
    def test_rebuild_0to10(self):
        """Run daos_test rebuild 0-10

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 0-10", 1500,
                                 "-s3 -u subtests='0-10'")

    def test_rebuild_12to15(self):
        """Run daos_test rebuild 12-15

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 12-15", 1500,
                                 "-s3 -u subtests='12-15'")

    def test_rebuild_16(self):
        """Run daos_test rebuild 16

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 16", 60,
                                 "-s3 -u subtests='16'")

    def test_rebuild_17(self):
        """Run daos_test rebuild 17

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 17", 60,
                                 "-s3 -u subtests='17'")

    @skipForTicket("DAOS-6442")
    def test_rebuild_18(self):
        """Run daos_test rebuild 18

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 18", 60,
                                 "-s3 -u subtests='18'")

    def test_rebuild_19(self):
        """Run daos_test rebuild 19

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 19", 60,
                                 "-s3 -u subtests='19'")

    def test_rebuild_20(self):
        """Run daos_test rebuild 20

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 20", 60,
                                 "-s3 -u subtests='20'")

    def test_rebuild_21(self):
        """Run daos_test rebuild 21

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 21", 60,
                                 "-s3 -u subtests='21'")

    def test_rebuild_22(self):
        """Run daos_test rebuild 22

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 22", 60,
                                 "-s3 -u subtests='22'")

    def test_rebuild_23(self):
        """Run daos_test rebuild 23

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 23", 60,
                                 "-s3 -u subtests='23'")

    def test_rebuild_24(self):
        """Run daos_test rebuild 24

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 24", 60,
                                 "-s3 -u subtests='24'")

    def test_rebuild_25(self):
        """Run daos_test rebuild 25

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 25", 60,
                                 "-s3 -u subtests='25'")

    def test_rebuild_26(self):
        """Run daos_test rebuild 26

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 26", 60,
                                 "-s3 -u subtests='26'")

    def test_rebuild_27(self):
        """Run daos_test rebuild 27

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 27", 1500,
                                 "-s6 -u subtests='27'")

    def test_rebuild_28(self):
        """Run daos_test rebuild 28

        :avocado: tags=all,pr,daily_regression,hw,medium,ib2,unittest
        :avocado: tags=daos_test_rebuild
        """
        DaosCoreBase.run_subtest(self, "r", "DAOS Rebuild 28", 60,
                                 "-s3 -u subtests='28'")
