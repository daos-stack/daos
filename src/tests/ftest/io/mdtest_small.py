#!/usr/bin/python
'''
  (C) Copyright 2019-2021 Intel Corporation.

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

        :avocado: tags=all,pr,daily_regression,hw,large,mdtest,mdtestsmall
        :avocado: tags=DAOS_5610
        """
        # local params
        mdtest_params = self.params.get("mdtest_params", "/run/mdtest/*")

        # Running mdtest for different variants
        for params in mdtest_params:
            # update mdtest params
            self.mdtest_cmd.api.update(params[0])
            self.mdtest_cmd.write_bytes.update(params[1])
            self.mdtest_cmd.read_bytes.update(params[2])
            self.mdtest_cmd.branching_factor.update(params[3])
            # if branching factor is 1 use num_of_files_dirs
            # else use items option of mdtest
            if params[3] == 1:
                self.mdtest_cmd.num_of_files_dirs.update(params[4])
            else:
                self.mdtest_cmd.items.update(params[4])
            self.mdtest_cmd.depth.update(params[5])
            self.mdtest_cmd.flags.update(params[6])
            # run mdtest
            self.execute_mdtest()
            # re-set mdtest params before next iteration
            self.mdtest_cmd.get_params(self)
