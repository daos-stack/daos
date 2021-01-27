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

class CartRpcOneNodeTest(TestWithoutServers):
    """
    Runs basic CaRT RPC tests

    :avocado: recursive
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def tearDown(self):
        """ Tear down """
        super(CartRpcOneNodeTest, self).tearDown()
        self.utils.cleanup_processes()

    def test_cart_rpc(self):
        """
        Test CaRT RPC

        :avocado: tags=all,cart,pr,daily_regression,rpc,one_node
        """
        srvcmd = self.utils.build_cmd(self, self.env, "test_servers")
        clicmd = self.utils.build_cmd(self, self.env, "test_clients")

        self.utils.launch_srv_cli_test(self, srvcmd, clicmd)
        self.utils.log_check(self)

if __name__ == "__main__":
    main()
