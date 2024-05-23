'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class CartCoRpcTwoNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run basic CaRT CoRPC tests.

    :avocado: recursive
    """

    def test_cart_corpc_two_node(self):
        """Test CaRT CoRPC.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=cart,corpc,two_node,memcheck
        :avocado: tags=CartCoRpcTwoNodeTest,test_cart_corpc_two_node
        """
        cmd = self.build_cmd(self.env, "test_servers")
        self.launch_test(cmd)
