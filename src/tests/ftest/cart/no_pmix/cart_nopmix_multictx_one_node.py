#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import subprocess

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
        pass_env = {
            "CRT_PHY_ADDR_STR": self.params.get(
                "CRT_PHY_ADDR_STR", '/run/defaultENV/'),
            "OFI_INTERFACE": self.params.get(
                "OFI_INTERFACE", '/run/defaultENV/'),
            "CRT_CTX_SHARE_ADDR": self.params.get(
                "CRT_CTX_SHARE_ADDR", '/run/defaultENV/'),
            "CRT_CTX_NUM": self.params.get(
                "CRT_CTX_NUM", '/run/defaultENV/')}

        cmd = self.params.get("tst_bin", '/run/tests/*/')

        self.print("\nTest cmd : {}\n".format(cmd))

        test_env = pass_env
        p = subprocess.Popen([cmd], env=test_env, stdout=subprocess.PIPE)

        rc = self.wait_process(p, 30)
        if rc != 0:
            self.print("Error waiting for process.")
            self.print("returning {}".format(rc))
            self.fail("Test failed.\n")

        self.print("Finished waiting for {}".format(p))
