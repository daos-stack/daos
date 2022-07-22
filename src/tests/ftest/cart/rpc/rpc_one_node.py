'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class CartRpcOneNodeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run basic CaRT RPC tests.

    :avocado: recursive
    """

    def test_cart_rpc(self):
        """Test CaRT RPC.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=cart,rpc,memcheck
        :avocado: tags=one_node,test_cart_rpc
        """
        srvcmd = self.build_cmd(self.env, "test_servers")
        clicmd = self.build_cmd(self.env, "test_clients")

        self.launch_srv_cli_test(srvcmd, clicmd)
        self.log_check()
