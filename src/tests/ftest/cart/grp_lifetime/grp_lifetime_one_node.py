'''
  (C) Copyright 2026 Gluesys Inc.
  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class CartGrpLifetimeTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run CaRT group lifetime test: RPC outliving group detach.

    :avocado: recursive
    """

    def test_cart_grp_lifetime(self):
        """Test CaRT RPC outliving secondary group destroy.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=cart,grp_lifetime,one_node,memcheck
        :avocado: tags=CartGrpLifetimeTest,test_cart_grp_lifetime
        """
        cmd = self.build_cmd(self.env, "test_servers")
        self.launch_test(cmd)
