#!/usr/bin/python
'''
  (C) Copyright 2018-2020 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from __future__ import print_function

import sys

from avocado       import Test
from avocado       import main

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class CartRpcOneNodeTest(Test):
    """
    Runs basic CaRT RPC tests

    :avocado: tags=all,cart,pr,rpc,one_node
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def test_cart_rpc(self):
        """
        Test CaRT RPC

        :avocado: tags=all,cart,pr,rpc,one_node
        """
        srvcmd = self.utils.build_cmd(self, self.env, "test_servers")
        clicmd = self.utils.build_cmd(self, self.env, "test_clients")

        self.utils.launch_srv_cli_test(self, srvcmd, clicmd)
        self.utils.log_check(self)

if __name__ == "__main__":
    main()
