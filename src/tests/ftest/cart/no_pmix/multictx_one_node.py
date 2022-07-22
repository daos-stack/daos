'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import subprocess  # nosec

from cart_utils import CartTest


class CartNoPmixOneNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run basic CaRT no_pmix tests.

    :avocado: recursive
    """

    def test_cart_no_pmix(self):
        """Test CaRT NoPmix.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=cart,memcheck
        :avocado: tags=no_pmix,one_node,test_cart_no_pmix
        """
        cmd = self.params.get("tst_bin", '/run/tests/*/')

        self.print("\nTest cmd : {}\n".format(cmd))

        with subprocess.Popen([cmd], stdout=subprocess.PIPE) as p:
            rc = self.wait_process(p, 30)
            if rc != 0:
                self.print("Error waiting for process.")
                self.print("returning {}".format(rc))
                self.fail("Test failed.\n")

            self.print("Finished waiting for {}".format(p))
