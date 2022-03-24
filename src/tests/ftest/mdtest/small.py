#!/usr/bin/python3
'''
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from mdtest_test_base import MdtestBase


class MdtestSmall(MdtestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs Mdtest with in small config.

    :avocado: recursive
    """

    def test_mdtest_small(self):
        """Jira ID: DAOS-2493.

        Test Description:
            Test Mdtest in small config.

        Use Cases:
            Aim of this test is to test different combinations
            of following configs:
            1/8 Clients
            num of files/dirs: 100
            with/without unique working dir for each task
            write bytes: 0|4K
            read bytes: 0|4K
            depth of hierarchical directory structure: 0|5

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=mdtest,checksum,mdtestsmall,mdtest
        :avocado: tags=DAOS_5610
        """
        # local params
        mdtest_params = self.params.get("mdtest_params", "/run/mdtest/*")
        # run mdtest
        self.run_mdtest_multiple_variants(mdtest_params)
