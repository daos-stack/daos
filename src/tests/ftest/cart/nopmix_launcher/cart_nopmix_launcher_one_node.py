#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from __future__ import print_function

import sys

from apricot       import TestWithoutServers

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class CartNoPmixLauncherOneNodeTest(TestWithoutServers):
    """
    Runs basic CaRT no_pmix_launcher tests

    :avocado: recursive
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def tearDown(self):
        """ Tear down """
        super(CartNoPmixLauncherOneNodeTest, self).tearDown()
        self.utils.cleanup_processes()

    def test_cart_no_pmix_launcher(self):
        """
        Test CaRT NoPmix Launcher

        :avocado: tags=all,cart,pr,daily_regression,no_pmix_launcher,one_node
        """

        cli_bin = self.params.get("test_clients_bin", '/run/tests/*/')
        cli_arg = self.params.get("test_clients_arg", '/run/tests/*/')
        cli_ppn = self.params.get("test_clients_ppn", '/run/tests/*/')
        log_mask = self.params.get("D_LOG_MASK", "/run/defaultENV/")
        crt_phy_addr = self.params.get("CRT_PHY_ADDR_STR",
                                       "/run/defaultENV/")
        ofi_interface = self.params.get("OFI_INTERFACE", "/run/defaultENV/")

        srv_cmd = self.utils.build_cmd(self, self.env, "test_servers")

        cmd = srv_cmd + " : -np {}".format(cli_ppn)
        cmd += " -x CRT_PHY_ADDR_STR={}".format(crt_phy_addr)
        cmd += " -x OFI_INTERFACE={}".format(ofi_interface)
        cmd += " -x D_LOG_MASK={}".format(log_mask)
        cmd += " {}".format(cli_bin)
        cmd += " {}".format(cli_arg)

        self.utils.launch_test(self, cmd)

if __name__ == "__main__":
    main()
