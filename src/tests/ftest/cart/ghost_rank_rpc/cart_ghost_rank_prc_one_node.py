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

class CartCoRpcOneNodeTest(TestWithoutServers):
    """
    Runs CaRT ghost rank RPC test

    :avocado: recursive
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        super(CartCoRpcOneNodeTest, self).setUp()
        self.utils = CartUtils()
        self.env = self.utils.get_env(self)

    def tearDown(self):
        """ Tear down """
        self.report_timeout()
        self._teardown_errors.extend(self.utils.cleanup_processes())
        super(CartCoRpcOneNodeTest, self).tearDown()

    def test_cart_ghost_rank_rpc(self):
        """
        Test ghost rank RPC

        #:avocado: tags=all,cart,pr,daily_regression,ghost_rank_rpc,one_node
        """

        cmd = self.utils.build_cmd(self, self.env, "test_servers")

        self.utils.launch_test(self, cmd)


if __name__ == "__main__":
    main()
