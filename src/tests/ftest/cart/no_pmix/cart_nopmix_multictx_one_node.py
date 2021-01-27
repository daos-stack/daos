#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from __future__ import print_function

import sys
import subprocess

from apricot       import TestWithoutServers

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
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)
        crt_phy_addr = self.params.get("CRT_PHY_ADDR_STR", '/run/defaultENV/')
        ofi_interface = self.params.get("OFI_INTERFACE", '/run/defaultENV/')
        ofi_ctx_num = self.params.get("CRT_CTX_NUM", '/run/defaultENV/')
        ofi_share_addr = self.params.get("CRT_CTX_SHARE_ADDR",
                                         '/run/defaultENV/')
        self.pass_env = {"CRT_PHY_ADDR_STR": crt_phy_addr,
                         "OFI_INTERFACE": ofi_interface,
                         "CRT_CTX_SHARE_ADDR": ofi_share_addr,
                         "CRT_CTX_NUM": ofi_ctx_num}

    def tearDown(self):
        """ Tear down """

        super(CartNoPmixOneNodeTest, self).tearDown()
        self.utils.cleanup_processes()

    def test_cart_no_pmix(self):
        """
        Test CaRT NoPmix

        :avocado: tags=all,cart,pr,daily_regression,no_pmix,one_node
        """

        cmd = self.params.get("tst_bin", '/run/tests/*/')

        self.utils.print("\nTest cmd : %s\n" % cmd)

        test_env = self.pass_env
        p = subprocess.Popen([cmd], env=test_env, stdout=subprocess.PIPE)

        rc = self.utils.wait_process(p, 10)
        if rc != 0:
            self.utils.print("Error waiting for process.")
            self.utils.print("returning {}".format(rc))
            self.fail("Test failed.\n")

        self.utils.print("Finished waiting for {}".format(p))


if __name__ == "__main__":
    main()
