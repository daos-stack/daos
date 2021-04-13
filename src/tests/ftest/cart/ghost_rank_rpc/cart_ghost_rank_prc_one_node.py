#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class CartCoRpcOneNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run CaRT ghost rank RPC test.

    :avocado: recursive
    """

    def test_cart_ghost_rank_rpc(self):
        """Test ghost rank RPC.

        #:avocado: tags=all,cart,pr,daily_regression,ghost_rank_rpc,one_node
        """
        cmd = self.build_cmd(self.env, "test_servers")
        self.launch_test(cmd)
