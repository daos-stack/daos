#!/usr/bin/python3
'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest

class CartGhostRankRpcOneNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run CaRT ghost rank RPC test.

    :avocado: recursive
    """

    def test_cart_ghost_rank_rpc(self):
        """Test ghost rank RPC.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=cart,ghost_rank_rpc,one_node,memcheck
        """
        cmd = self.build_cmd(self.env, "test_servers")
        self.launch_test(cmd)
