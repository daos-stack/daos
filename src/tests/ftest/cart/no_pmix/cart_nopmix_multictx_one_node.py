#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import subprocess
import os

from cart_utils import CartTest


class CartNoPmixOneNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run basic CaRT no_pmix tests.

    :avocado: recursive
    """

    def test_cart_no_pmix(self):
        """Test CaRT NoPmix.

        :avocado: tags=all,cart,pr,daily_regression,no_pmix,one_node
        """
        crt_phy_addr = os.environ.get("CRT_PHY_ADDR_STR")
        ofi_interface = os.environ.get("OFI_INTERFACE")
        ofi_ctx_num = os.environ.get("CRT_CTX_NUM")
        ofi_share_addr = os.environ.get("CRT_CTX_SHARE_ADDR")

        # env dict keys and values must all be of string type
        if not isinstance(crt_phy_addr, ("".__class__, u"".__class__)):
            crt_phy_addr = ""
        if not isinstance(ofi_interface, ("".__class__, u"".__class__)):
            ofi_interface = ""
        if not isinstance(ofi_ctx_num, ("".__class__, u"".__class__)):
            ofi_ctx_num = ""
        if not isinstance(ofi_share_addr, ("".__class__, u"".__class__)):
            ofi_share_addr = ""

        test_env = {"CRT_PHY_ADDR_STR": crt_phy_addr,
                    "OFI_INTERFACE": ofi_interface,
                    "CRT_CTX_SHARE_ADDR": ofi_share_addr,
                    "CRT_CTX_NUM": ofi_ctx_num}

        cmd = self.params.get("tst_bin", '/run/tests/*/')

        self.print("\nTest cmd : {}\n".format(cmd))

        p = subprocess.Popen([cmd], env=test_env, stdout=subprocess.PIPE)

        rc = self.wait_process(p, 30)
        if rc != 0:
            self.print("Error waiting for process.")
            self.print("returning {}".format(rc))
            self.fail("Test failed.\n")

        self.print("Finished waiting for {}".format(p))
