#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import os
from cart_utils import CartTest


class CartNoPmixLauncherOneNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run basic CaRT no_pmix_launcher tests.

    :avocado: recursive
    """

    def test_cart_no_pmix_launcher(self):
        """Test CaRT NoPmix Launcher.

        :avocado: tags=all,cart,pr,daily_regression,no_pmix_launcher,one_node
        """
        cli_bin = self.params.get("test_clients_bin", '/run/tests/*/')
        cli_arg = self.params.get("test_clients_arg", '/run/tests/*/')
        cli_ppn = self.params.get("test_clients_ppn", '/run/tests/*/')

        log_mask = os.environ.get("D_LOG_MASK")
        crt_phy_addr = os.environ.get("CRT_PHY_ADDR_STR")
        ofi_interface = os.environ.get("OFI_INTERFACE")

        srv_cmd = self.build_cmd(self.env, "test_servers")

        cmd = srv_cmd + " : -np {}".format(cli_ppn)
        cmd += " -x CRT_PHY_ADDR_STR={}".format(crt_phy_addr)
        cmd += " -x OFI_INTERFACE={}".format(ofi_interface)
        cmd += " -x D_LOG_MASK={}".format(log_mask)
        cmd += " {}".format(cli_bin)
        cmd += " {}".format(cli_arg)

        self.launch_test(cmd)
