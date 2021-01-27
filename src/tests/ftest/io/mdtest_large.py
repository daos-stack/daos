#!/usr/bin/python
'''
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from mdtest_test_base import MdtestBase

class MdtestLarge(MdtestBase):
    """
    Class for mdtest with large configurations
    :avocado: recursive
    """

    def test_mdtest_large(self):
        """
        Jira ID: DAOS-2494
        Test Description:
            Test Mdtest for large config.
        Use Cases:
            Aim of this test is to test different combinations
            of following configs for performance purpose:
            Servers: 1 | 8
            Clients: 1 | 64 | 128
            num of files/dirs: 10000
            iter: 3
            with/without unique working dir for each task
            write bytes: 0 | 1K | 32K
            read bytes: 0 | 1K | 32K
            depth of hierarchical directory structure: 0 | 100
        :avocado: tags=all,hw,perf,nvme,mdtest,mdtestlarge
        """
        mdtest_flags = self.params.get("flags", "/run/mdtest/*")
        self.mdtest_cmd.flags.update(mdtest_flags)
        self.execute_mdtest()
