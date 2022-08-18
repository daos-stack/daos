#!/usr/bin/python3
'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from cart_utils import CartTest


class GroupTest(CartTest):
    # pylint: disable=too-few-public-methods
    """Run GroupTests for primary and secondary groups.

    :avocado: recursive
    """

    def test_group(self):
        """Test CaRT NoPmix Launcher.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=cart,group_test,one_node,memcheck
        """
        cmd = self.build_cmd(self.env, "test_servers")
        self.launch_test(cmd)
