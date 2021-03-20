#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class GroupTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run GroupTests for primary and secondary resizeable groups.

    :avocado: recursive
    """

    def test_group(self):
        """Test CaRT NoPmix Launcher.

        :avocado: tags=all,cart,pr,daily_regression,group_test,one_node
        """
        cmd = self.build_cmd(self.env, "test_servers")
        self.launch_test(cmd)
