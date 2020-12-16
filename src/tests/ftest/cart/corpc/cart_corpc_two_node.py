#!/usr/bin/python
'''
  (C) Copyright 2018-2019 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from __future__ import print_function

import sys

from avocado  import Test
from avocado  import main

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class CartCoRpcTwoNodeTest(Test):
    """
    Runs basic CaRT CoRPC tests

    :avocado: tags=all,cart,pr,corpc,two_node
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def test_cart_corpc(self):
        """
        Test CaRT CoRPC

        :avocado: tags=all,cart,pr,corpc,two_node
        """

        cmd = self.utils.build_cmd(self, self.env, "test_servers")

        self.utils.launch_test(self, cmd)


if __name__ == "__main__":
    main()
