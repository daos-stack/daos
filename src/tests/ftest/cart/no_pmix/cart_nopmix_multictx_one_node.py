#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from __future__ import print_function

import sys
import subprocess
import os

from apricot import TestWithoutServers

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class CartNoPmixOneNodeTest(TestWithoutServers):
    """
    Runs basic CaRT no_pmix tests

    :avocado: recursive
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        super(CartNoPmixOneNodeTest, self).setUp()
        self.utils = CartUtils()

    def tearDown(self):
        """ Tear down """
        self.report_timeout()
        self._teardown_errors.extend(self.utils.cleanup_processes())
        super(CartNoPmixOneNodeTest, self).tearDown()

    def test_cart_no_pmix(self):
        """
        Test CaRT NoPmix

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

        pass_env = {"CRT_PHY_ADDR_STR": crt_phy_addr,
                    "OFI_INTERFACE": ofi_interface,
                    "CRT_CTX_SHARE_ADDR": ofi_share_addr,
                    "CRT_CTX_NUM": ofi_ctx_num}

        cmd = self.params.get("tst_bin", '/run/tests/*/')

        self.utils.print("\nTest cmd : %s\n" % cmd)

        test_env = pass_env
        p = subprocess.Popen([cmd], env=test_env, stdout=subprocess.PIPE)

        rc = self.utils.wait_process(p, 30)
        if rc != 0:
            self.utils.print("Error waiting for process.")
            self.utils.print("returning {}".format(rc))
            self.fail("Test failed.\n")

        self.utils.print("Finished waiting for {}".format(p))
