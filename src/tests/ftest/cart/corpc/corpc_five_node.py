'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class CartCoRpcFiveNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run basic CaRT CoRPC tests.

    :avocado: recursive
    """

    def test_cart_corpc(self):
        """Test CaRT CoRPC.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=cart,memcheck
        :avocado: tags=corpc,five_node,test_cart_corpc
        """
        cmd = self.build_cmd(self.env, "test_servers")
        self.launch_test(cmd)
