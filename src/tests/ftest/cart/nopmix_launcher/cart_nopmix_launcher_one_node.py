#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from __future__ import print_function

import os
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
        super(CartNoPmixLauncherOneNodeTest, self).setUp()
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def tearDown(self):
        """ Tear down """
        self.report_timeout()
        self._teardown_errors.extend(self.utils.cleanup_processes())
        super(CartNoPmixLauncherOneNodeTest, self).tearDown()

    def test_cart_no_pmix_launcher(self):
        """
        Test CaRT NoPmix Launcher

        :avocado: tags=all,cart,pr,daily_regression,no_pmix_launcher,one_node
        """

        cli_bin = self.params.get("test_clients_bin", '/run/tests/*/')
        cli_arg = self.params.get("test_clients_arg", '/run/tests/*/')
        cli_ppn = self.params.get("test_clients_ppn", '/run/tests/*/')

        log_mask = os.environ.get("D_LOG_MASK")
        crt_phy_addr = os.environ.get("CRT_PHY_ADDR_STR")
        ofi_interface = os.environ.get("OFI_INTERFACE")

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
