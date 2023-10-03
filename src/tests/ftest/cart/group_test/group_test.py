'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class CartGroupTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run GroupTests for primary and secondary groups.

    :avocado: recursive
    """

    def test_cart_group(self):
        """Test CaRT NoPmix Launcher.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=cart,group_test,one_node,memcheck
        :avocado: tags=CartGroupTest,test_cart_group
        """
        cmd = self.build_cmd(self.env, "test_servers")
        self.launch_test(cmd)
